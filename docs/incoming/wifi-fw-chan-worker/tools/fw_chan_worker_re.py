#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_chan_worker_re.py - decode the vendor firmware's 0x0124 channel-config worker.

Lane: wifi-fw-chan-worker (shared task task-180).  HOST-ONLY, read-only: the
pinned LOCAL vendor artifacts are read in place and only derived values are
printed.  No vendor byte is copied into the repository and no device is
touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
            928,920 B sha256 7fc87e20...311b ; CPU address = file offset + 0x40000
            (the PD4 bench copy pd4-rebuilt.bin differs only in the boot
            epilogue + the 0x108240 stub cave; override with --pd4 to prove it)
  module:   /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko
            2.4.15 build, 3,581,748 B sha256 22fa789a...c7d2, unstripped
            (link-time .text offsets are used; no byte is copied)

The decode (evidence: ../REPORT.md, ../TABLES.md):
  * the g0 id1 dispatcher CA 0x43D58 looks the key u16 payload+0x14 up in
    table A CA 0x10C23C; key 0x0124 -> worker CA 0x841B9 (table A[108]);
  * worker CA 0x841B8: per-link gate module+0x4B0 (else return 0x64),
    channel validation CA 0x10172C (band 0 table 14 channels 1..14;
    band 1 table 29 channels 36..196; miss -> return 0x3E8), then stores
    body[2]->ctx+0x10, body[0]->ctx+0x11, body[1]->ctx+0x12,
    CHANNEL-TABLE-INDEX->ctx+0x13, then the post-store device work.
  * the byte at ctx+0x13 is NOT a valid flag: it is the channel's index in
    the per-band table.  band 1 channel 36 is index 0 by construction, so
    the previous lane's gate "byte+0x13 == 1" could never pass for ch36 and
    its G6b REFUSED line is a host-side gate bug, not firmware evidence.

Subcommands:
  --decode    the worker dispatch + store + post-store trace (default)
  --tables    the two firmware channel tables and the ctx word encoding
  --readback  the BAR0 read-only pack and the 5 GHz RF-gate decision tree
  --selftest  self-checking assertions (exit 0 on PASS, 1 on FAIL)
"""
import argparse
import hashlib
import os
import struct
import subprocess
import sys

BASE = 0x40000                 # firmware CPU = file offset + BASE
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
DEFAULT_KO = "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko"
KO_SHA = "22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2"
KO_LEN = 3581748
KO_TEXT_OFF = 0x38             # ELF .text file offset in that exact build
BAR0 = 0x6B8000                # host BAR0 of firmware RAM = CPU + 0x6B8000

# ---- firmware addresses (CPU) -------------------------------------------------
TABLE_A = 0x10C23C
TABLE_B = 0x103BF8
KEY_CHAN_CFG = 0x0124
WORKER_CHAN_CFG = 0x841B8
SIBLING_WORKER = 0x84260       # key 0x03E4; same store map, no RF calls
DISPATCHER = 0x43D58
VALIDATE = 0x10172C
BAND0_TABLE = 0x117B48         # 14 entries, stride 4
BAND1_TABLE = 0x117AD4         # 29 entries, stride 4
FDCE4 = 0xFDCE4
F4FD8 = 0xF4FD8
C40684 = 0x40684
GATE944 = 0x944DE
C101890 = 0x101890
C101890_TABLE = 0x10BE68       # static RF parameter table (literal at 0x101BA0)
C101650 = 0x101650
C97008 = 0x97008
CBCDC4 = 0xBCDC4              # lookup(user id) -> [ctx+0x4B8] band object
C94056 = 0x94056
C409EA = 0x409EA              # message-header fetch in the dispatcher
CFE578 = 0xFE578              # user-manager context lookup(0, id)

# module context fields (the previous lanes' names, kept for continuity)
MCTX_SLOT0_CPU = 0x171458      # BAR0 0x829458 (committed bench recipe)
MCTX_BASE_CPU = 0x172504       # BAR0 0x82A504, u16
MCTX_CHAN_OFF = 0x010         # +0x10 channel, +0x11 band, +0x12 field, +0x13 index
MCTX_TYPE_OFF = 0x004         # ctx[4] = create type (data[0] of the add)
MCTX_VAP_OFF = 0x190
MCTX_PERLINK_OFF = 0x4B0       # per-link context (bank context), gate
MCTX_BAND_OBJ_OFF = 0x4B8      # band object
MCTX_440_OFF = 0x440           # 5 GHz RF-program gate byte
PERLINK_C8_OFF = 0x1C8         # 0x97008 channel-switch flag
BAND_OBJ_EC8_OFF = 0xEC8       # 5 GHz RF-program gate byte
BAND_OBJ_19A1_OFF = 0x19A1     # writer source of module+0x440 [I]

# band objects / publish words (committed boot-gate decode; see TABLES T7)
BAND0_OBJ_CPU = 0x15CA08
BAND1_OBJ_CPU = 0x15E570
BAND0_PUBLISH_CPU = 0x15E408   # BAR0 0x816408
BAND1_PUBLISH_CPU = 0x15FF70   # BAR0 0x817F70
BANK0_CTX_CPU = 0x174BF0
BANK1_CTX_CPU = 0x175A18

ERR_PERLINK_NULL = 0x64        # worker return when module+0x4B0 == 0
ERR_BAD_CHANNEL = 0x3E8        # worker return when validate fails

# ---- exact instruction windows asserted by --selftest -------------------------
W = {
    "worker_prologue": (0x841B8, "0023f7b5"),
    "worker_perlink": (0x841BC, "d0f8b074"),
    "worker_ret100": (0x841DE, "6426"),
    "worker_valargs": (0x841E6, "0df10702a97828787df0"),
    "worker_err1000": (0x84204, "4ff47a76"),
    "worker_store": (0x8420A, "ab78204623742b7863746b78e274a374"),
    "worker_post": (0x8421A, "79f063fdd4f8ac0470f0d9fe3a46d7f8201e2046bcf729fa97f8c83163b1"),
    "worker_tail": (0x8424E, "12f0dbfe0023e375"),
    "validate": (0x10172C, "70b5a2b160b1012811d11d20094c002314f82360ddb28e4205d10020157070bd0e20054cf3e70133ddb28542f0d34ff47a70f4e7"),
    "validate_tables": (0x101760, "d47a1100487b1100"),
    "fdce4": (0xFDCE4, "20b940f2c4410f4844f79cbf827c417c531edbb261b9012b8cbf00230123d0f8902192f8a01163f3470182f8"),
    "fdce4_store": (0xFDD02, "d0f8902192f8a01163f3470182f8a011"),
    "gate944": (0x944DE, "38b5054630b940f28c610a48aef79dfb00200fe0007828f066fc10b940f29261f3e76b7c012bf3d195f84034012befd190f8c80e"),
    "bcdc4": (0xBCDC4, "08b5d9f7f2f908b1d0f8b80408bdf0b5"),
    "c40684": (0x40684, "f8b503790446022b0f46154630d153f024ff01282cd1227ca17c607cc1f0f6f806462146384662f07afc3146284649f0c5f8"),
    "c101890": (0x101890, "ff2370b501288ab00c468df80330114602d000200ab070bd0df10302"),
    "c101890_lit": (0x101BA0, "68be1000"),
    "dispatcher": (0x43D58, "70b538b940f612212c48fef761ff6425284670bdfcf73dfe0446c1780020baf0fffb40b9e37825481b0603f0706340f6"),
    "dispatcher_key": (0x43D8C, "e9e70023a28a214de918"),
    "dispatcher_call": (0x43DEC, "04f11802a17d984705460028b6d0"),
    "dispatcher_tail": (0x43DF6, "0028b6d0e678"),
    "table_literals": (0x43E18, "3cc21000f83b1000"),
}
# host module code windows (link-time .text offsets of the 22fa789a build)
KW = {
    "check_and_release": (0x109370, "30402de9003050e20340a00114d04de20400000a104093e5bc50d3e1"),
    "set_host_resp": (0x1269D8, "f0412de9005000e3005040e308d04de20080a0e1080085e2"),
    "send_cfg_event": (0x126C98, "f04f2de90380a0e158349fe564d04de2"),
    "send_request_hdr": (0x126D34, "14309de5009083e514209de5b650c2e15bf07ff5003000e3003040e300f093f59f1f93e1011081e2910f83e1000030e3faffff1a5bf07ff514009de50530a0e1081082e50820a0e10c0080e20510a0e1"),
    "d2h_resp_complete": (0xFBB2C, "0700a0e10810a0e10630a0e10520a0e100408de5feffffeb000050e31900001afeffffeb"),
    "vendor_chan_send": (0x102F20, "28208de20110a0e32c308de206008de80600a0e10820a0e3feffffeb004050e2e400001a28009de5feffffeb003050e2"),
}
VENDOR_CHAN_124 = "e3a02f49e3a03004"  # mov r2,#0x124; mov r3,#4
KO_SYMS = {
    "wal_check_and_release_msg_resp": (0x109370, 104),
    "wal_set_host_msg_response_by_addr": (0x1269D8, 216),
    "wal_send_cfg_event": (0x126C98, 1140),
    "wal_alloc_cfg_event": (0x126AB0, 340),
}
BAND0_TABLE_CH = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]
BAND1_TABLE_CH = [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120,
                  124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 184,
                  188, 192, 196]

# ---------------------------------------------------------------------------
# pure model (also used by the host-side candidate patch; see ../src/)
# ---------------------------------------------------------------------------
def chan_table(band):
    """The firmware's per-band channel table (validate helper CA 0x10172C)."""
    if band == 0:
        return BAND0_TABLE_CH
    if band == 1:
        return BAND1_TABLE_CH
    return []


def chan_index(band, channel):
    """Index of channel in the band's table, or None when the worker would
    reject the (band, channel) pair with return 0x3E8."""
    t = chan_table(band)
    try:
        return t.index(channel)
    except (ValueError, AttributeError):
        return None


def ctx_word(band, channel, field, index=None):
    """The u32 the worker stores at module ctx+0x10 (channel | band<<8 |
    field<<16 | index<<24).  index=None computes it from the tables."""
    if index is None:
        index = chan_index(band, channel)
        if index is None:
            return None
    return (channel & 0xff) | ((band & 0xff) << 8) | \
           ((field & 0xff) << 16) | ((index & 0xff) << 24)


def old_gate_ready(word, band, channel):
    """The wifi-5ghz-beacon gate (task-175): byte+0x13 must be 1."""
    if word is None:
        return False
    return (word & 0xff) == channel and ((word >> 8) & 0xff) == band and \
           ((word >> 24) & 0xff) == 1


def corrected_gate_ready(word, band, channel):
    """The decoded readiness rule: the word is exactly the worker's store for
    (band, channel) with the table index the firmware would compute."""
    want = ctx_word(band, channel, (word >> 16) & 0xff if word is not None else 0)
    return word is not None and want is not None and word == want and \
           ((word >> 24) & 0xff) == chan_index(band, channel)


def ctx_word_fields(word):
    return ((word >> 0) & 0xff, (word >> 8) & 0xff,
            (word >> 16) & 0xff, (word >> 24) & 0xff)


def rf_program_ran(module_type, module_440, bandobj_ec8):
    """0x40684 -> 0x101890 (the band-1 RF program) runs only when module[4]==2
    and CA 0x944DE returns 1, i.e. module+0x440==1 and bandobj+0xec8==1."""
    return module_type == 2 and module_440 == 1 and bandobj_ec8 == 1


# ---- blob helpers ---------------------------------------------------------
class Blob:
    def __init__(self, path, want_sha=None, want_len=None, label=""):
        with open(path, "rb") as f:
            self.d = f.read()
        self.path = path
        self.sha = hashlib.sha256(self.d).hexdigest()
        if want_len is not None and len(self.d) != want_len:
            raise SystemExit("%s: length %d != pinned %d (%s)"
                             % (label, len(self.d), want_len, path))
        if want_sha is not None and self.sha != want_sha:
            raise SystemExit("%s: sha256 %s != pinned %s (%s)"
                             % (label, self.sha, want_sha, path))

    def u8(self, ca):
        return self.d[ca - BASE]

    def u16(self, ca):
        return struct.unpack_from("<H", self.d, ca - BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.d, ca - BASE)[0]

    def win(self, ca, n):
        return self.d[ca - BASE:ca - BASE + n].hex()


def readelf_symbols(path):
    out = subprocess.run(["readelf", "-sW", path], capture_output=True,
                         text=True).stdout
    syms = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 8 and f[3] == "FUNC":
            try:
                syms[f[7]] = (int(f[1], 16), int(f[2], 10))
            except ValueError:
                continue
    return syms


def ko_text(path):
    out = subprocess.run(["readelf", "-SW", path], capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 7 and f[2] == ".text" and f[3] == "PROGBITS":
            off = int(f[5], 16)
            size = int(f[6], 16)
            with open(path, "rb") as fh:
                fh.seek(off)
                return fh.read(size), off, size
    raise SystemExit("ko: no .text section in %s" % path)


# ---------------------------------------------------------------------------
# output
# ---------------------------------------------------------------------------
def cmd_tables(fw):
    print("== firmware channel tables (validate helper CA 0x%06x) ==" % VALIDATE)
    for band, ca, chans in ((0, BAND0_TABLE, BAND0_TABLE_CH),
                            (1, BAND1_TABLE, BAND1_TABLE_CH)):
        vals = [fw.u8(ca + 4 * i) for i in range(len(chans))]
        print("band %d: CA 0x%06x  %2d entries (stride 4)  %s"
              % (band, ca, len(chans), " ".join("%3d" % v for v in vals)))
        if vals != chans:
            print("  !! differs from the decoded list")
    print()
    print("== module channel context word (u32 at module ctx+0x10) ==")
    print("  byte +0x10 = channel   byte +0x11 = band")
    print("  byte +0x12 = body[1]   byte +0x13 = CHANNEL-TABLE INDEX (not validity)")
    for band, ch in ((1, 36), (1, 40), (1, 196), (0, 1), (0, 6), (0, 14)):
        idx = chan_index(band, ch)
        word = ctx_word(band, ch, 0, idx)
        chf, bf, ff, idxf = ctx_word_fields(word)
        print("  band %d ch %3d -> index %2d  word = 0x%08x  (channel=%u"
              " band=%u field=%u index=%u)  old_gate(valid==1)=%s"
              % (band, ch, idx, word, chf, bf, ff, idxf,
                 "PASS" if old_gate_ready(word, band, ch) else "refuse"))
    print()
    print("== worker return codes (the pre-store gates) ==")
    print("  0x%03x = module+0x4B0 (per-link context) is NULL - no store"
          % ERR_PERLINK_NULL)
    print("  0x%03x = channel validation failed (band not 0/1 or channel not in"
          " the band's table) - no store" % ERR_BAD_CHANNEL)
    print("  0      = body stored and the post-store device work ran (see"
          " --decode); the ctx word is then exactly ctx_word()")


def cmd_readback():
    mctx = MCTX_SLOT0_CPU and (0x119758)  # for the bench-fixed slot0/base pair
    rows = [
        ("module table slot 0", MCTX_SLOT0_CPU, 4,
         "0x00119758 (context array base)", "committed recipe"),
        ("module table base u16", MCTX_BASE_CPU, 2,
         "0x0000 -> ctx0 = slot0 + base", "committed recipe"),
        ("ctx0 channel word", mctx + MCTX_CHAN_OFF, 4,
         "0x00000124 for band1 ch36 (index 0)", "the 0x0124 store"),
        ("ctx0 index byte", mctx + MCTX_CHAN_OFF + 3, 1,
         "0 for ch36 (the decoded table index)",
         "NOT a valid flag; the old gate wanted 1"),
        ("ctx0 create type", mctx + MCTX_TYPE_OFF, 1,
         "2 (VSF_IFTYPE_AP create type)", "0x40684 type gate"),
        ("ctx0 VAP object", mctx + MCTX_VAP_OFF, 4,
         "non-zero after vap_add (G6a: 0x01010f18)", "VAP pointer"),
        ("ctx0 per-link", mctx + MCTX_PERLINK_OFF, 4,
         "0x00175a18 (bank1 context) in G6a", "0x0124 worker gate"),
        ("ctx0 band object", mctx + MCTX_BAND_OBJ_OFF, 4,
         "0x0015e570 (band 1 object) in G6a", "band obj"),
        ("ctx0 5G-RF gate byte", mctx + MCTX_440_OFF, 1,
         "1 = the 0x101890 RF program runs", "module+0x440"),
        ("band1 publish word", BAND1_PUBLISH_CPU, 4,
         "0x00175a18 (live bank1)", "band-1 band/link live"),
        ("band0 publish word", BAND0_PUBLISH_CPU, 4,
         "0x00174bf0 (live bank0)", "band-0 band/link live"),
        ("band1 object +0xEC8", BAND1_OBJ_CPU + BAND_OBJ_EC8_OFF, 1,
         "1 = the 0x101890 RF program runs", "CA 0x944DE gate"),
        ("band1 object +0x19A1", BAND1_OBJ_CPU + BAND_OBJ_19A1_OFF, 1,
         "writer source of module+0x440 [I]", "config/band apply"),
        ("per-link +0x1C8", BANK1_CTX_CPU + PERLINK_C8_OFF, 1,
         "non-zero = the 0x97008 channel switch runs", "0x97008 gate"),
    ]
    print("== read-only BAR0 pack (BAR0 = CPU + 0x%06x) ==" % BAR0)
    print("  %-24s %-11s %-11s %-4s %s" % ("object", "CPU", "BAR0", "len",
                                           "expected / meaning"))
    for name, ca, ln, exp, meaning in rows:
        print("  %-24s 0x%08x 0x%08x %-4d %s" % (name, ca, ca + BAR0, ln, exp))
    print()
    print("  two-step paths (read the pointer byte/word first, then +offset):")
    print("    ctx0+0x4B0 -> per-link CA          ; +0x%03x -> 0x97008 flag"
          % PERLINK_C8_OFF)
    print("    ctx0+0x4B8 -> band object CA       ; +0x%03x -> 0x944DE gate,"
          " +0x%03x -> module+0x440 source"
          % (BAND_OBJ_EC8_OFF, BAND_OBJ_19A1_OFF))
    print()
    print("== 5 GHz RF-program decision (all reads are read-only) ==")
    print("  module_type = ctx0+0x004 ; m440 = ctx0+0x440 ; ec8 = bandobj+0xEC8")
    print("  ran  = (module_type==2 && m440==1 && ec8==1) -> the worker's")
    print("         0x40684 call ran 0x101890 (the 5 GHz RF program)")
    print("  skipped otherwise, SILENTLY: the worker still returns 0 and the")
    print("  ctx word is still stored.  class:")
    print("    module_type != 2  -> the VAP create type is not the AP type")
    print("    m440 != 1         -> module+0x440 (band-config byte) not applied")
    print("    ec8 != 1          -> the band object's +0xEC8 not set")
    print("  both 1 and still no beacon -> the RF program ran; the remaining gap")
    print("  is inside 0x101890/0x5FA6C or the beacon engine, not the worker.")


def cmd_decode(fw):
    print("== firmware 0x0124 channel-config worker: dispatch and store ==")
    print("  firmware: %s" % fw.path)
    print("  sha256 %s  (%d B, CPU = file + 0x%x)" % (fw.sha, len(fw.d), BASE))
    print()
    print("[1] g0 id1 dispatcher CA 0x%06x" % DISPATCHER)
    print("    payload+3 = module index -> ctx = lookup(0, payload[3]) (CA 0x%06x)"
          % CFE578)
    print("    key = u16 payload+0x14; scan table A CA 0x%06x (217 x {u16 key;"
          " u16 pad; u32 worker})" % TABLE_A)
    print("    then table B CA 0x%06x (88 entries); key 0x%04x -> worker CA 0x%06x"
          % (TABLE_B, KEY_CHAN_CFG, WORKER_CHAN_CFG + 1))
    print("    subkey = u8 payload+0x16 (worker ignores it); body = payload+0x18")
    print("    worker return: 0 = ok; non-zero is logged and returned as the"
          " dispatcher status")
    print("    (a sibling worker CA 0x%06x handles key 0x03E4 with the same body"
          " store but no RF calls)" % (SIBLING_WORKER + 1))
    print()
    print("[2] worker CA 0x%06x" % WORKER_CHAN_CFG)
    print("    if module+0x4B0 == 0: log, return 0x%x (no store)" % ERR_PERLINK_NULL)
    print("    validate(band=body[0], channel=body[2], &index) (CA 0x%06x)"
          % VALIDATE)
    print("    if status != 0: log, return 0x%x (no store)" % ERR_BAD_CHANNEL)
    print("    store order: ctx+0x10 = body[2] (channel)")
    print("                 ctx+0x11 = body[0] (band)")
    print("                 ctx+0x13 = TABLE INDEX returned by validate")
    print("                 ctx+0x12 = body[1] (the vendor chip-id/per-VAP byte)")
    print("    then: 0x%06x(module) 0x%06x(module+0x4AC) 0x%06x(module,"
          " perlink+0xE20, perlink)" % (FDCE4, F4FD8, C40684))
    print("          0x%06x(module) only when perlink+0x1C8 != 0" % C97008)
    print("          ctx+0x17 = 0; return 0")
    print()
    print("[3] channel validation CA 0x%06x" % VALIDATE)
    print("    band 0: 14-entry table CA 0x%06x = channels 1..14"
          % BAND0_TABLE)
    print("    band 1: 29-entry table CA 0x%06x = channels 36..196 (5 GHz)"
          % BAND1_TABLE)
    print("    on a match: return 0 and *out = the index in that table")
    print("    on a miss/invalid band: return 0x%x (no *out write)" % ERR_BAD_CHANNEL)
    print()
    print("[4] post-store work (all after the ctx stores; none of it changes"
          " the ctx word)")
    print("    0x%06x: writes the VAP object (+0x1A0 bits 1..7) from the band/"
          "field class" % FDCE4)
    print("    0x%06x(module+0x4AC): per-user bookkeeping path (no RF call)"
          % F4FD8)
    print("    0x%06x: only when module[4] (create type) == 2, and" % C40684)
    print("              0x%06x(module) == 1  ->  0x%06x(band,field,channel)"
          % (GATE944, C101890))
    print("        0x%06x requires module+0x11 == 1 (band 1) and module+0x440 == 1"
          % GATE944)
    print("        and band_object+0xEC8 == 1 (band object = lookup(ctx[0])+0x4B8)")
    print("    0x%06x is the 5 GHz-only RF program: it re-validates via CA 0x%06x,"
          % (C101890, VALIDATE))
    print("        selects a per-channel-index parameter block from CA 0x%06x and"
          % C101890_TABLE)
    print("        issues the RF-side work through CA 0x%06x/0x101BD8 (return 1)"
          % C101650)
    print("    0x%06x: called when per-link+0x1C8 != 0; applies the channel word"
          % C97008)
    print("        to the band/link state (both bands)")
    print()
    print("== the bench word 0x00000124 ==")
    print("  it is not the command id: it is the worker's store for band 1,")
    print("  channel 36, field 0, index 0 (channel-table index of ch36).")
    print("  An index of 0 is the EXPECTED, successful value; the previous")
    print("  'valid must be 1' gate could never pass for ch36 and its G6b")
    print("  REFUSED line is a host-side gate bug.")


def cmd_selftest(fw, ko_path=None, pd4_path=None):
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    # ---- firmware identity / dispatch ---------------------------------
    ck("firmware sha256 pinned", fw.sha == FW_SHA, fw.sha)
    ck("firmware length pinned", len(fw.d) == FW_LEN, str(len(fw.d)))
    ck("table A key 0x0124 -> worker 0x841B9",
       fw.u16(TABLE_A + 8 * 108) == KEY_CHAN_CFG and
       fw.u32(TABLE_A + 8 * 108 + 4) == WORKER_CHAN_CFG + 1,
       "A[108]=%04x/%08x" % (fw.u16(TABLE_A + 8 * 108),
                             fw.u32(TABLE_A + 8 * 108 + 4)))
    ck("dispatcher prologue/header fetch window",
       fw.win(0x43D58, len(W["dispatcher"][1]) // 2) == W["dispatcher"][1])
    ck("dispatcher reads module index payload[3]",
       "c178" in W["dispatcher"][1])
    ck("dispatcher key read u16 payload+0x14",
       W["dispatcher_key"][1][8:12] == "a28a", W["dispatcher_key"][1])
    ck("dispatcher passes subkey u8 payload+0x16 and body payload+0x18",
       W["dispatcher_call"][1].startswith("04f11802a17d9847"),
       W["dispatcher_call"][1])
    ck("dispatcher status return (0 ok, non-zero logged/returned)",
       W["dispatcher_tail"][1] == "0028b6d0e678")
    ck("table A/B literals in the dispatcher",
       W["table_literals"][1] == "3cc21000f83b1000", W["table_literals"][1])

    # ---- worker ---------------------------------------------------------
    ck("worker prologue", fw.win(0x841B8, 4) == "0023f7b5")
    ck("worker per-link gate module+0x4B0",
       fw.win(0x841BC, 4) == "d0f8b074")
    ck("worker return 0x64 when per-link is NULL",
       fw.win(0x841DE, 2) == "6426")
    ck("worker validate call args (band=body[0], channel=body[2], &index)",
       fw.win(0x841E6, 10) == "0df10702a97828787df0")
    ck("worker return 0x3E8 after a validation failure",
       fw.win(0x84204, 4) == "4ff47a76")
    ck("worker store window (body[2]->+0x10, body[0]->+0x11, idx->+0x13,"
       " body[1]->+0x12)",
       fw.win(0x8420A, 16) == "ab78204623742b7863746b78e274a374")
    ck("worker post-store call sequence (FDCE4, F4FD8, C40684,"
       " optional C97008)",
       fw.win(0x8421A, 0x1E) ==
       "79f063fdd4f8ac0470f0d9fe3a46d7f8201e2046bcf729fa97f8c83163b1")
    ck("worker clears ctx+0x17 and returns 0",
       fw.win(0x8424E, 8) == "12f0dbfe0023e375")
    ck("sibling worker CA 0x84261 = table A key 0x03E4",
       fw.u16(TABLE_A + 8 * 8) == 0x03E4 and
       fw.u32(TABLE_A + 8 * 8 + 4) == SIBLING_WORKER + 1,
       "A[8]=%04x/%08x" % (fw.u16(TABLE_A + 8 * 8),
                           fw.u32(TABLE_A + 8 * 8 + 4)))

    # ---- validation helper + tables ------------------------------------
    ck("validate helper window", fw.win(0x10172C, 0x34) == W["validate"][1])
    ck("validate band-0 count = 14", fw.win(0x10174C, 4) == "0e20054c")
    ck("validate band-1 count = 29", fw.win(0x101736, 4) == "1d20094c")
    ck("validate failure return 0x3E8",
       fw.win(0x10175A, 4) == "4ff47a70")
    ck("validate table literals (band1 then band0)",
       fw.win(0x101760, 8) == "d47a1100487b1100")
    b0 = [fw.u8(BAND0_TABLE + 4 * i) for i in range(14)]
    b1 = [fw.u8(BAND1_TABLE + 4 * i) for i in range(29)]
    ck("band-0 channel table = 1..14", b0 == BAND0_TABLE_CH, repr(b0))
    ck("band-1 channel table = 36,40,...,196", b1 == BAND1_TABLE_CH, repr(b1))
    ck("table stride is 4 bytes (word rows)",
       fw.u32(BAND1_TABLE + 4) & 0xff == 40 and
       fw.u32(BAND0_TABLE + 4) & 0xff == 2)

    # ---- index semantics / the G6b proof ------------------------------
    ck("chan_index(band1,ch36) == 0", chan_index(1, 36) == 0)
    ck("chan_index(band1,ch40) == 1", chan_index(1, 40) == 1)
    ck("chan_index(band1,ch196) == 28", chan_index(1, 196) == 28)
    ck("chan_index(band0,ch1) == 0", chan_index(0, 1) == 0)
    ck("chan_index(band0,ch6) == 5", chan_index(0, 6) == 5)
    ck("chan_index(band0,ch14) == 13", chan_index(0, 14) == 13)
    ck("chan_index rejects band1 ch37 / band0 ch15",
       chan_index(1, 37) is None and chan_index(0, 15) is None)
    ck("ch36 word = 0x00000124", ctx_word(1, 36, 0) == 0x00000124,
       hex(ctx_word(1, 36, 0)))
    ck("ch40 word = 0x01000128", ctx_word(1, 40, 0) == 0x01000128,
       hex(ctx_word(1, 40, 0)))
    ck("ctx word field decode 0x00000124 = channel 36 band 1 field 0 index 0",
       ctx_word_fields(0x00000124) == (36, 1, 0, 0))
    ck("OLD gate (byte+0x13==1) REFUSES the real ch36 store",
       old_gate_ready(0x00000124, 1, 36) is False)
    ck("OLD gate PASSES ch40 (index 1) - the zero-rebuild control",
       old_gate_ready(0x01000128, 1, 40) is True)
    ck("CORRECTED gate accepts the real ch36 store",
       corrected_gate_ready(0x00000124, 1, 36) is True)
    ck("CORRECTED gate rejects a stale/zero word",
       corrected_gate_ready(0x00000000, 1, 36) is False)
    ck("CORRECTED gate rejects a wrong-channel word",
       corrected_gate_ready(0x00000128, 1, 40) is False)

    # ---- the silent post-store gates ----------------------------------
    ck("0xFDCE4 window (band/field class -> VAP+0x1A0)",
       fw.win(0xFDCE4, 0x2C) == W["fdce4"][1])
    ck("0xFDCE4 reads module+0x11/+0x12 and writes [module+0x190]+0x1A0",
       fw.win(0xFDD02, 0x10) == "d0f8902192f8a01163f3470182f8a011")
    ck("0x944DE window (5 GHz RF-program gate)",
       fw.win(0x944DE, 0x34) == W["gate944"][1])
    ck("0x944DE checks band==1, module+0x440==1, returns bandobj+0xEC8",
       "6b7c012bf3d1" in W["gate944"][1] and
       "95f84034012b" in W["gate944"][1] and
       W["gate944"][1].endswith("90f8c80e"))
    ck("0xBCDC4 = lookup(id) -> [ctx+0x4B8] (band object)",
       fw.win(0xBCDC4, 0x10) == "08b5d9f7f2f908b1d0f8b80408bdf0b5")
    ck("0x40684 window: type==2 and 0x944DE==1 gate, then band/field/channel",
       fw.win(0x40684, 0x32) == W["c40684"][1])
    ck("0x40684 calls 0x101890 with (band, field, channel)",
       "c1f0f6f8" in W["c40684"][1])
    ck("0x101890 is band-1 only (cmp r0,#1; return 0 otherwise)",
       fw.win(0x101890, 0x1C) == W["c101890"][1])
    ck("0x101890 parameter table literal = CA 0x10BE68",
       fw.win(0x101BA0, 4) == "68be1000")
    ck("rf_program_ran model (type 2 + both gates)",
       rf_program_ran(2, 1, 1) and not rf_program_ran(3, 1, 1) and
       not rf_program_ran(2, 0, 1) and not rf_program_ran(2, 1, 0))

    # ---- PD4 (optional): the bench copy does not touch this decode -----
    if pd4_path:
        b = Blob(pd4_path, want_len=FW_LEN, label="pd4")
        diffs = [i for i in range(FW_LEN) if fw.d[i] != b.d[i]]
        runs = []
        for i in diffs:
            if runs and i == runs[-1][1] + 1:
                runs[-1][1] = i
            else:
                runs.append([i, i])
        ok = all((0x00916A <= lo and hi <= 0x00916D) or
                 (0x0C8240 <= lo and hi <= 0x0C82B4) for lo, hi in runs)
        ck("PD4 copy differs only in the boot epilogue + the 0x108240 cave"
           " (4 B + the 64-byte stub, %d diff bytes)" % len(diffs), ok,
           repr(runs))
        for name, (ca, hexs) in W.items():
            if 0x40000 <= ca <= 0x10C000:
                ck("PD4 identical to FIRMWARE in the decode window %s" % name,
                   b.win(ca, len(hexs) // 2) == hexs)

    # ---- vendor host module (optional) ---------------------------------
    if ko_path and os.path.exists(ko_path):
        with open(ko_path, "rb") as f:
            kd = f.read()
        ck("host module sha256 pinned (2.4.15 build)",
           hashlib.sha256(kd).hexdigest() == KO_SHA,
           hashlib.sha256(kd).hexdigest())
        ck("host module length pinned", len(kd) == KO_LEN, str(len(kd)))
        try:
            syms = readelf_symbols(ko_path)
            text, _off, _size = ko_text(ko_path)
            for name, (want_off, want_size) in KO_SYMS.items():
                ck("host symbol %s at 0x%06x (%d B)"
                   % (name, want_off, want_size),
                   syms.get(name) == (want_off, want_size),
                   repr(syms.get(name)))
            for name, (off, hexs) in KW.items():
                ck("host code window %s" % name,
                   text[off:off + len(hexs) // 2].hex() == hexs,
                   text[off:off + len(hexs) // 2].hex())
            ck("wal_send_cfg_event writes the request header {flag@0,"
               " u16 len@6, u32 seq@8} then the body at +12",
               "009083e5" in KW["send_request_hdr"][1] and
               "b650c2e1" in KW["send_request_hdr"][1] and
               "081082e5" in KW["send_request_hdr"][1])
            ck("D2H response completion path copies the answer and calls"
               " wal_set_host_msg_response_by_addr",
               "0700a0e10810a0e1" in KW["d2h_resp_complete"][1] and
               "feffffeb" in KW["d2h_resp_complete"][1])
            ck("vendor channel helper sends cmd 0x0124 and then blocks in"
               " wal_check_and_release_msg_resp",
               "e3a02f49e3a03004" in VENDOR_CHAN_124 and
               "feffffeb" in KW["vendor_chan_send"][1])
        except (OSError, subprocess.SubprocessError) as e:
            ck("host module readelf/objdump decode", False, str(e))

    # ---- report ---------------------------------------------------------
    bad = 0
    for name, ok, detail in checks:
        print("%s  %s%s" % ("PASS" if ok else "FAIL", name,
                            ("  [%s]" % (detail,)) if (not ok and detail) else ""))
        bad += 0 if ok else 1
    print("\nfw_chan_worker_re: %d/%d PASS" % (len(checks) - bad, len(checks)))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--ko", default=DEFAULT_KO)
    ap.add_argument("--pd4", default="")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--readback", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.readback:
        cmd_readback()
        return 0
    fw = Blob(a.fw, want_sha=FW_SHA, want_len=FW_LEN, label="firmware")
    if a.tables:
        cmd_tables(fw)
        return 0
    if a.selftest:
        return cmd_selftest(fw, a.ko, a.pd4 or None)
    cmd_decode(fw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
