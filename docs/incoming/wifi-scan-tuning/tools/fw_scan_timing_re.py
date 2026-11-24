#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_scan_timing_re.py - regenerable static evidence for the wifi-scan-tuning
lane (shared task-133).  Reads the pinned LOCAL vendor firmware in place and
prints derived annotations only; no firmware byte, vendor object byte or
calibration value is committed.

Address convention: firmware CPU address (CA) = file offset + 0x40000; Thumb-2.

Evidence asserted (all [V] in ../REPORT.md sec 3-4):
  F1 g0 id0xC handler CA 0x6494C tail-calls the scan engine CA 0x644A4.
  F2 engine copies the 0x1FC request to VAP+0x300 and sets VAP+0x51C=1.
  F3 the request channel count is read as u8 from VAP+0x4EC (payload+0x1EC);
     the engine then iterates count 4-B entries (end = VAP + count*4).
  F4 the per-channel scheduler CA 0x63CB8 uses request entry
     index = link[0xFC] + link[0xFD].
  F5 the result/advance loop stops when link[0xFD] >= link[0xFE]
     (CA 0x64298) and writes a hard-coded 6 into link[0x106] at CA 0x643CC.
  F6 g1 id17 is emitted from CA 0x642D0..0x64374: hcc_msg_tx call at
     CA 0x64358, 0x6C-byte record copy at CA 0x64350.
  F7 the scan/channel-context config handler is **g0 id10**: the group-0 direct
     H2D dispatch table at CA 0x10CD08 (12-B rows {id, fn|1, 0}) has row
     {id=10, fn=0x5FF65} -> handler entry CA 0x5FF64.  It initializes the
     per-slot scan-context fields (context+0xFC/+0xFE/+0x103/+0x104/+0x106)
     from an H2D payload at CA 0x60092/0x600B2/0x603C6/0x603DC.
  F8 the firmware's own single-channel request builder CA 0x4B88C writes
     count=1 / scan_type=4 / dwell=20 into its internal request.
  F9 the scan request path reads payload+0x1EE dwell (ldrh) at CA 0x6477A
     and payload+0x1F0 scan_type at CA 0x64600; 4/8 are rejected at entry.
"""
import argparse
import hashlib
import struct
import os
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
except Exception as exc:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python): %s" % exc,
          file=sys.stderr)
    raise SystemExit(2)

FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
FW_BASE = 0x40000

# CA anchors
A_HANDLER = 0x6494C
A_ENGINE = 0x644A4
A_COUNT_READ = 0x6462A
A_COUNT_END = 0x64636
A_LOOP_CMP = 0x6463A
A_SCHEDULER = 0x63CB8
A_SCHED_IDX_FD = 0x63D10
A_SCHED_IDX_FC = 0x63D14
A_ADVANCE = 0x64298
A_DEFAULT6 = 0x643CC
A_ID17_TX = 0x64358
A_ID17_COPY = 0x64350
A_BANDCFG = 0x5FF64  # handler entry (was 0x5FF60 from a 4-byte-misaligned window)
A_BANDCFG_FC = 0x603DC
A_BANDCFG_FE = 0x603C6
A_PERCHAN_BUILD = 0x4B88C
A_PERCHAN_COUNT = 0x4B91C
A_PERCHAN_TYPE = 0x4B918
A_PERCHAN_DWELL = 0x4B936
A_ENGINE_DWELL = 0x6477A
A_ENGINE_TYPE = 0x64600


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "..", "..", ".."))


def resolve(env):
    cands = [os.environ.get(env),
             os.path.join(repo_root(), "rootfs-2.5.24/lib/firmware/hi_wifi/FIRMWARE.bin"),
             os.path.join(repo_root(), "rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin")]
    for c in cands:
        if c and os.path.isfile(c):
            return c
    return None


class Fw(object):
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    def win(self, ca, n):
        return self.d[ca - FW_BASE:ca - FW_BASE + n]

    def insn(self, ca):
        for i in self.md.disasm(self.win(ca, 4), ca):
            return i
        return None


FMT = "0x%x: %-8s %s"
checks = [0, 0]


def ck(cond, msg):
    checks[0] += 1
    if not cond:
        checks[1] += 1
        print("FAIL " + msg)
    else:
        print("ok   " + msg)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fw", default=None, help="local vendor FIRMWARE.bin (never committed)")
    args = ap.parse_args(argv)
    path = args.fw or resolve("HI5622_FIRMWARE")
    if not path:
        print("error: vendor firmware not found (set HI5622_FIRMWARE or --fw)", file=sys.stderr)
        return 2
    fw = Fw(path)
    print("firmware: %s" % path)
    print("  sha256: %s len: %d%s" % (fw.sha256, len(fw.d),
          "" if (fw.sha256 == FW_SHA256 and len(fw.d) == FW_LEN) else " (not the pinned artifact)"))
    print()

    i = fw.insn(0x649CE)
    ck(i is not None and i.mnemonic == "b" and i.op_str == "#0x644a4",
       "F1 handler CA 0x6494C tail-calls the engine: " + (FMT % (i.address, i.mnemonic, i.op_str) if i else "?"))
    i = fw.insn(0x645A8)
    ck(i is not None and "r3, #0x1fc" in i.op_str, "F2 0x1FC-byte request copy at CA 0x645A8")
    i = fw.insn(0x645A4)
    ck(i is not None and "[r4, #0x51c]" in i.op_str, "F2 VAP+0x51C=1 busy flag at CA 0x645A4")
    i = fw.insn(A_COUNT_READ)
    ck(i is not None and i.mnemonic.startswith("ldrb") and "[r4, #0x4ec]" in i.op_str,
       "F3 count read u8 VAP+0x4EC (payload+0x1EC) at CA 0x6462A")
    i = fw.insn(A_COUNT_END)
    ck(i is not None and i.mnemonic.startswith("add") and "lsl #2" in i.op_str,
       "F3 end = VAP + count*4 at CA 0x64636")
    i = fw.insn(0x646AE)
    ck(i is not None and "[r1, #0x441]" in i.op_str,
       "F3 per-entry band byte read [VAP+4i+0x441] at CA 0x646AE")
    i = fw.insn(A_ENGINE_DWELL)
    ck(i is not None and i.mnemonic.startswith("ldrh") and "[r4, #0x4ee]" in i.op_str,
       "F9 dwell read u16 VAP+0x4EE (payload+0x1EE) at CA 0x6477A")
    i = fw.insn(A_ENGINE_TYPE)
    ck(i is not None and "[r4, #0x4f0]" in i.op_str,
       "F9 scan_type read payload+0x1F0 at CA 0x64600")
    i = fw.insn(0x644B0)
    ck(i is not None and "#4" in i.op_str, "F9 scan_type 4 rejected at CA 0x644B0")
    i = fw.insn(A_SCHED_IDX_FD)
    ck(i is not None and "[r5, #0xfd]" in i.op_str, "F4 scheduler cursor link+0xFD at CA 0x63D10")
    i = fw.insn(A_SCHED_IDX_FC)
    ck(i is not None and "[r5, #0xfc]" in i.op_str, "F4 scheduler base link+0xFC at CA 0x63D14")
    i = fw.insn(A_ADVANCE)
    ck(i is not None and "[r4, #0xfd]" in i.op_str,
       "F5 advance loop reads link[0xFD] at CA 0x64298")
    i = fw.insn(0x6429C)
    ck(i is not None and "[r4, #0xfe]" in i.op_str,
       "F5 advance loop reads link[0xFE] at CA 0x6429C")
    i = fw.insn(A_DEFAULT6)
    ck(i is not None and i.mnemonic.startswith("mov") and "#6" in i.op_str,
       "F5 default 6 constant at CA 0x643CC")
    i = fw.insn(0x643CE)
    ck(i is not None and "[r4, #0x106]" in i.op_str,
       "F5 default 6 stored to link+0x106 at CA 0x643CE")
    i = fw.insn(A_ID17_COPY)
    ck(i is not None and "#0x6c" in i.op_str, "F6 g1 id17 0x6C-byte record copy at CA 0x64350")
    i = fw.insn(A_ID17_TX)
    ck(i is not None and i.mnemonic.startswith("bl"), "F6 hcc_msg_tx call site at CA 0x64358")
    i = fw.insn(A_BANDCFG_FC)
    ck(i is not None and "[r3, #0xfc]" in i.op_str, "F7 band-config context +0xFC write at CA 0x603DC")
    i = fw.insn(A_BANDCFG_FE)
    ck(i is not None and "[r3, #0xfe]" in i.op_str, "F7 band-config context +0xFE write at CA 0x603C6")
    i = fw.insn(A_PERCHAN_COUNT)
    ck(i is not None and i.mnemonic.startswith("strh") and "[sp, #0x1f0]" in i.op_str,
       "F8 firmware internal request count store at CA 0x4B91C")
    i = fw.insn(A_PERCHAN_TYPE)
    ck(i is not None and i.mnemonic.startswith("strb") and "[sp, #0x1f4]" in i.op_str,
       "F8 firmware internal scan_type store at CA 0x4B918")
    i = fw.insn(0x4B914)
    ck(i is not None and "#4" in i.op_str,
       "F8 firmware internal scan_type=4 constant at CA 0x4B914")
    i = fw.insn(A_PERCHAN_DWELL)
    ck(i is not None and i.mnemonic.startswith("strh") and "[sp, #0x1f2]" in i.op_str,
       "F8 firmware internal dwell store at CA 0x4B936")
    i = fw.insn(0x4B934)
    ck(i is not None and "#0x14" in i.op_str,
       "F8 firmware internal dwell=20 constant at CA 0x4B934")
    # F7b: the g0 id10 dispatch row and the handler entry
    table = 0x10CD08
    row = None
    for k in range(40):
        eid = struct.unpack_from("<I", fw.d, table - FW_BASE + 12 * k)[0]
        efn = struct.unpack_from("<I", fw.d, table - FW_BASE + 12 * k + 4)[0]
        if eid == 0 and efn == 0:
            continue
        if eid == 10:
            row = (k, eid, efn)
    ck(row is not None and row[2] == 0x5FF65,
       "F7b group-0 dispatch table row id=10 -> handler thumb 0x5FF65")
    i = fw.insn(A_BANDCFG)
    ck(i is not None and i.mnemonic.startswith("push"),
       "F7b handler entry CA 0x5FF64 is a Thumb function (push.w)")
    i = fw.insn(0x5FFA4)
    ck(i is not None and "[r4, #0xc]" in i.op_str,
       "F7b g0 id10 payload+0x00 gate (==3) read at CA 0x5FFA4")
    i = fw.insn(0x5FFAC)
    ck(i is not None and "[r4, #0xd]" in i.op_str,
       "F7b g0 id10 payload+0x01 subtype read at CA 0x5FFAC")
    i = fw.insn(0x60092)
    ck(i is not None and "[sb, #0x104]" in i.op_str,
       "F7b context +0x104 write at CA 0x60092")
    i = fw.insn(0x6009E)
    ck(i is not None and "[sb, #0x106]" in i.op_str,
       "F7b context +0x106 write at CA 0x6009E")
    i = fw.insn(0x600B2)
    ck(i is not None and "[r2, #0xfc]" in i.op_str,
       "F7b context +0xFC write at CA 0x600B2")

    print()
    print("fw_scan_timing_re: %d/%d checks %s (fw %s)" %
          (checks[0] - checks[1], checks[0],
           "PASS" if checks[1] == 0 else "FAIL", fw.sha256[:16]))
    return 0 if checks[1] == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
