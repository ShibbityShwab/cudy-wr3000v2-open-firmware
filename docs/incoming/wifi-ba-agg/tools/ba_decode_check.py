#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ba_decode_check.py - regenerable static evidence for lane wifi-ba-agg
(shared task-178).

Reads the LOCAL vendor host module and firmware in place and asserts every
address / field-map anchor cited in ../TABLES.md.  Prints only addresses and
derived annotations; no vendor or firmware byte is copied into the repo.

Usage:
    ba_decode_check.py --ko /path/hi5622v100_wifi.ko --fw /path/FIRMWARE.bin

The host-module disassembly checks use the project ARM objdump
(--objdump or CROSS_OBJDUMP; the selectorfix toolchain is the default).
The firmware handler checks use capstone when importable, otherwise SKIP.
Exit code 0 iff no FAIL.  The committed ../evidence/static-checks.txt is this
tool's output on the pinned artifacts.
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
FW_BASE = 0x40000
G0_TABLE = 0x10CCFC

CHECKS = [0, 0]  # total, failed


def ck(cond, msg):
    CHECKS[0] += 1
    if not cond:
        CHECKS[1] += 1
        print("FAIL " + msg)
    else:
        print("ok   " + msg)


class Elf:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        self.secs = {}
        self.syms = {}
        self._read_sections()
        self._read_symbols()

    def _readelf(self, *args):
        out = subprocess.run(["readelf"] + list(args), capture_output=True,
                             text=True, check=False)
        return out.stdout

    def _read_sections(self):
        txt = self._readelf("-SW", self.path)
        pat = re.compile(r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+"
                         r"([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)")
        for ln in txt.splitlines():
            m = pat.match(ln)
            if m:
                self.secs[m.group(1)] = {
                    "type": m.group(2),
                    "addr": int(m.group(3), 16),
                    "off": int(m.group(4), 16),
                    "size": int(m.group(5), 16),
                }

    def _read_symbols(self):
        txt = self._readelf("-sW", self.path)
        pat = re.compile(r"^\s*(\d+):\s+([0-9a-f]+)\s+(\d+)\s+(\S+)\s+"
                         r"(\S+)\s+(\S+)\s+(\S+)\s*(.*)$")
        for ln in txt.splitlines():
            m = pat.match(ln)
            if not m:
                continue
            name = m.group(8).strip()
            if not name:
                continue
            self.syms[name] = {
                "value": int(m.group(2), 16),
                "size": int(m.group(3)),
                "type": m.group(4),
                "ndx": m.group(7),
            }

    def sym(self, name):
        return self.syms.get(name)

    def relocs(self, relname):
        """dict: offset -> symbol name, parsed from readelf -rW."""
        txt = self._readelf("-rW", self.path)
        want = "Relocation section '%s'" % relname
        sec = None
        out = {}
        pat = re.compile(r"^([0-9a-f]{8})\s+[0-9a-f]{8}\s+(\S+)\s+"
                         r"([0-9a-f]*)\s*(.*)$")
        for ln in txt.splitlines():
            m = re.match(r"Relocation section '(\S+)'", ln)
            if m:
                sec = m.group(1)
                continue
            if sec != relname:
                continue
            m = pat.match(ln)
            if m:
                out[int(m.group(1), 16)] = m.group(4).strip()
        return out

    def word(self, secname, off):
        s = self.secs[secname]
        return struct.unpack_from("<I", self.data, s["off"] + off)[0]

    def dis(self, start, stop, objdump):
        out = subprocess.run(
            [objdump, "-d", "--no-show-raw-insn",
             "--start-address=0x%x" % start,
             "--stop-address=0x%x" % stop, self.path],
            capture_output=True, text=True, check=False)
        return out.stdout


def find_objdump(explicit):
    if explicit:
        return explicit
    env = os.environ.get("CROSS_OBJDUMP")
    if env:
        return env
    cand = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
            "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
            "arm-openwrt-linux-objdump")
    if os.path.isfile(cand):
        return cand
    sys.exit("error: ARM objdump not found (use --objdump or CROSS_OBJDUMP)")


def ck_regs(dis, regex, msg):
    ck(re.search(regex, dis) is not None, msg)


def check_symbols(ko):
    table = [
        ("hmac_ba_rx_init", 0xdb580, 0x58),
        ("hmac_ba_rx_win_init", 0xdb440, 0x140),
        ("hmac_ba_rx_add", 0xdb6e0, 0x208),
        ("hmac_ba_rx_del", 0xdb5d8, 0x108),
        ("hmac_ba_rx_buffered_data_software_check", 0xdbcb8, 0x308),
        ("hmac_ba_timeout_fn", 0xdb208, 0x170),
        ("hmac_ba_timeout_proc", 0xdca80, 0x6b4),
        ("hmac_ba_tx_init", 0xdb8e8, 0x60),
        ("hmac_ba_tx_add", 0xdb948, 0x78),
        ("hmac_ba_tx_del", 0xdba4c, 0x84),
        ("hdpp_user_rx_init", 0x1d64, 0xdc),
        ("hdpp_tx_tid_init", 0x13ca4, 0x38),
        ("hmac_tx_ba_setup", 0xaf180, 0x104),
        ("hmac_tx_ba_session_process", 0x16cf0, 0x340),
        ("hmac_tx_ba_notify", 0x16b90, 0x160),
        ("hmac_create_ba_event", 0x3ece4, 0x1c4),
        ("hmac_del_ba_event", 0x3eea8, 0x244),
        ("hmac_mgmt_rx_delba_event", 0x49220, 0x208),
        ("hmac_ba_action_msg_process", 0xaf284, 0x278),
        ("hmac_mgmt_rx_addba_req", 0x45588, 0x2b8),
        ("hmac_mgmt_rx_addba_rsp", 0x45a84, 0x2f4),
        ("hmac_mgmt_rx_addba_rsp_verify", 0x45888, 0x1fc),
        ("hmac_mgmt_rx_delba", 0x45d78, 0x34c),
        ("hmac_mgmt_tx_addba_req", 0x44734, 0x278),
        ("hmac_mgmt_tx_addba_rsp", 0x44a70, 0x328),
        ("hmac_mgmt_tx_delba", 0x44f30, 0x248),
        ("hmac_mgmt_encap_addba_req", 0x43e50, 0x198),
        ("hmac_mgmt_encap_addba_rsp", 0x43fe8, 0x19c),
        ("hmac_mgmt_encap_delba", 0x44184, 0x16c),
        ("hmac_mgmt_tx_action", 0x490e4, 0x13c),
        ("hmac_mgmt_tx_ampdu_start", 0x4626c, 0x1c8),
        ("hmac_mgmt_tx_ampdu_end", 0x46434, 0x184),
        ("hmac_mgmt_tx_priv_req", 0x48ef0, 0xc8),
        ("hmac_update_user_tx_addba_req", 0x44454, 0x21c),
        ("hmac_set_addba_req_para_sync", 0x44670, 0xc4),
        ("hmac_config_addba_req", 0x5b6fc, 0x210),
        ("hmac_config_delba_req", 0x5c4cc, 0x254),
        ("hmac_config_set_baw_size", 0x5ac3c, 0x150),
        ("hmac_config_send_bar", 0x57f98, 0x84),
        ("hmac_config_dump_ba_bitmap", 0x57cb8, 0x90),
        ("hmac_config_set_ampdu_aggr_num", 0x579d8, 0x5c),
        ("hmac_config_ampdu_start", 0x56318, 0x8),
        ("hmac_config_ampdu_end", 0x5b070, 0x1b0),
        ("hdpp_user_sync_write", 0x2a18, 0x154),
        ("wal_config_addba_req", 0x1620ac, 4),
        ("wal_config_delba_req", 0x1620a8, 4),
        ("wal_config_set_baw_size", 0x1620a4, 4),
    ]
    for name, addr, size in table:
        s = ko.sym(name)
        ck(s is not None and s["value"] == addr and s["size"] == size,
           "symbol %-38s .text 0x%05x size 0x%x" % (name, addr, size))


def check_d2h_table(ko):
    want = [
        ("hmac_create_ba_event", 2),
        ("hmac_del_ba_event", 3),
        ("hmac_mgmt_rx_delba_event", 0x0f),
        ("hmac_ba_action_msg_process", 0x1d),
    ]
    rel = ko.relocs(".rel.data")
    rev = {}
    for off, name in rel.items():
        rev.setdefault(name, off)
    for name, msgid in want:
        off = rev.get(name)
        got = ko.word(".data", off + 8) if off is not None else None
        ck(off is not None and got == msgid,
           "D2H dispatch row %-28s id 0x%02x" % (name, msgid))


def check_wal_table(ko):
    want = [
        ("wal_config_addba_req", 0x0ba),
        ("wal_config_delba_req", 0x324),
        ("wal_config_set_baw_size", 0x32c),
    ]
    rel = ko.relocs(".rel.rodata")
    rev = {}
    for off, name in rel.items():
        rev.setdefault(name, off)
    for name, cmd in want:
        off = rev.get(name)
        got = ko.word(".rodata", off + 4) if off is not None else None
        ck(off is not None and got == cmd,
           "WAL config row %-28s cmd 0x%03x" % (name, cmd))


def check_host_disasm(ko, objdump):
    def get(name):
        s = ko.sym(name)
        return s["value"], s["value"] + s["size"]

    def dis(name):
        start, stop = get(name)
        return ko.dis(start, stop, objdump)

    d = dis("hmac_mgmt_encap_addba_req")
    ck_regs(d, r"mov\s+r4, #208", "encap req: FC 0x00d0 immediate")
    ck_regs(d, r"strh\s+r4, \[r1\]", "encap req: FC stored at frame[0]")
    ck_regs(d, r"strb\s+r0, \[r1, #24\]", "encap req: category at frame[24]")
    ck_regs(d, r"mov\s+r0, #3", "encap req: category 3")
    ck_regs(d, r"strb\s+ip, \[r1, #25\]", "encap req: action at frame[25]")
    ck_regs(d, r"mov\s+r0, #33", "encap req: total 33 bytes")
    ck_regs(d, r"strb\s+r3, \[r1, #28\]", "encap req: BA param high byte at 28")
    ck_regs(d, r"ldrb\s+r4, \[r5, #2686\]",
            "encap req: peer MAC source user+0xa7e (DA)")
    ck_regs(d, r"ldr\s+lr, \[lr, #308\]",
            "encap req: own SA via +0x1134 pointer")
    ck_regs(d, r"ldrb\s+ip, \[r0, #3944\]",
            "encap req: BSSID source +0xf68")
    ck_regs(d, r"ldrb\s+r0, \[r2, #5\]", "encap req: dialog token source")

    d = dis("hmac_mgmt_encap_addba_rsp")
    ck_regs(d, r"strb\s+r0, \[r1, #25\]", "encap rsp: action at frame[25]")
    ck_regs(d, r"mov\s+r0, #1", "encap rsp: action 1")
    ck_regs(d, r"mov\s+r0, #33", "encap rsp: total 33 bytes")
    ck_regs(d, r"strb\s+r5, \[r1, #27\]", "encap rsp: status at frame[27]")

    d = dis("hmac_mgmt_encap_delba")
    ck_regs(d, r"mov\s+ip, #208", "encap delba: FC 0x00d0 immediate")
    ck_regs(d, r"strb\s+r7, \[r1, #28\]", "encap delba: reason at frame[28]")
    ck_regs(d, r"strb\s+r3, \[r1, #27\]", "encap delba: param byte at frame[27]")
    ck_regs(d, r"mov\s+r0, #30", "encap delba: total 30 bytes")

    d = dis("hmac_mgmt_rx_addba_rsp")
    ck_regs(d, r"movw\s+r0, #259", "relay rsp: 0x0103 type/subtype")
    ck_regs(d, r"mov\s+r2, #10", "relay rsp: HCC id 0x0a")
    ck_regs(d, r"strb\s+r2, \[r3, #6\]", "relay rsp: hdr[6] = id")
    ck_regs(d, r"strh\s+r0, \[r3, #12\]", "relay rsp: payload+0 type/subtype")
    ck_regs(d, r"strh\s+r2, \[r3, #14\]", "relay rsp: payload+2 user index")
    ck_regs(d, r"strb\s+r2, \[r3, #19\]", "relay rsp: payload+7 TID")
    ck_regs(d, r"strb\s+r2, \[r3, #21\]", "relay rsp: payload+9 status")
    ck_regs(d, r"strh\s+r2, \[r3, #24\]", "relay rsp: payload+0x0c BAW u16")
    ck_regs(d, r"strb\s+r2, \[r3, #32\]", "relay rsp: payload+0x14 dialog")
    ck_regs(d, r"strb\s+r2, \[r3, #33\]", "relay rsp: payload+0x15 policy")
    ck_regs(d, r"movweq\s+r1, #3908", "relay rsp: BAW clamp user+0xf44")
    ck_regs(d, r"movwne\s+r1, #3910", "relay rsp: BAW clamp user+0xf46")
    ck_regs(d, r"bl\s+0 <hcc_msg_alloc>",
            "relay rsp: allocator (32-byte payload)")
    ck_regs(d, r"mov\s+r0, #32", "relay rsp: hcc_msg_alloc(32)")

    d = dis("hmac_mgmt_rx_delba")
    ck_regs(d, r"movw\s+r1, #515", "relay delba: 0x0203 type/subtype")
    ck_regs(d, r"mov\s+r3, #10", "relay delba: HCC id 0x0a")
    ck_regs(d, r"strb\s+r9, \[r5, #19\]", "relay delba: payload+7 TID")
    ck_regs(d, r"strb\s+r7, \[r5, #20\]", "relay delba: payload+8 initiator")

    d = dis("hmac_mgmt_tx_ampdu_start")
    ck_regs(d, r"mov\s+r2, #11", "ampdu start: HCC id 0x0b")
    ck_regs(d, r"strb\s+r2, \[r3, #6\]", "ampdu start: hdr[6] = id")
    ck_regs(d, r"strh\s+r2, \[r3, #14\]", "ampdu start: payload+2 seq")
    ck_regs(d, r"strh\s+r2, \[r3, #16\]", "ampdu start: payload+4 user key")
    ck_regs(d, r"strb\s+r2, \[r3, #18\]", "ampdu start: payload+6 flow flag")

    d = dis("hmac_mgmt_tx_ampdu_end")
    ck_regs(d, r"mov\s+r3, #11", "ampdu end: HCC id 0x0b")
    ck_regs(d, r"strb\s+r1, \[r4, #12\]", "ampdu end: payload+0 = 1")
    ck_regs(d, r"strh\s+r3, \[r4, #16\]", "ampdu end: payload+4 user key")

    d = dis("hmac_tx_ba_notify")
    ck_regs(d, r"mov\s+r3, #28", "tx notify: HCC id 0x1c")
    ck_regs(d, r"strb\s+r3, \[r4, #6\]", "tx notify: hdr[6] = id")
    ck_regs(d, r"strh\s+r3, \[r4, #12\]", "tx notify: payload+0 uid u16")
    ck_regs(d, r"strb\s+r8, \[r4, #15\]", "tx notify: payload+3 value")

    d = dis("hmac_ba_timeout_fn")
    ck_regs(d, r"mov\s+r3, #8", "ba timeout: core msg id 8")
    ck_regs(d, r"strb\s+r3, \[r4, #6\]", "ba timeout: hdr[6] = 8")
    ck_regs(d, r"mov\s+r0, #4", "ba timeout: hcc_msg_alloc(4) cookie")
    ck_regs(d, r"bl\s+0 <hcc_msg_tx_to_core>",
            "ba timeout: hcc_msg_tx_to_core")

    d = dis("hmac_ba_rx_add")
    ck_regs(d, r"movw\s+r2, #6180", "rx add: window alloc 0x1824")
    ck_regs(d, r"movw\s+r3, #6180", "rx add: window memset 0x1824")
    ck_regs(d, r"strb\s+r6, \[r3, #21\]", "rx add: window state +0x15 = 2")

    d = dis("hmac_ba_rx_win_init")
    ck_regs(d, r"strh\s+r2, \[r5\]", "win init: start seq +0")
    ck_regs(d, r"strh\s+r3, \[r5, #10\]", "win init: BAW +0x0a")
    ck_regs(d, r"strh\s+ip, \[r5, #22\]", "win init: pending count +0x16")
    ck_regs(d, r"add\s+r3, r3, #24", "win init: 24-byte reorder slots")

    d = dis("hdpp_user_rx_init")
    ck_regs(d, r"addne\s+r8, r7, #1648", "rx init: user+0x670 base")
    ck_regs(d, r"add\s+r4, r4, #128", "rx init: 0x80 RX session stride")
    ck_regs(d, r"bl\s+db580 <hmac_ba_rx_init>", "rx init: 8x hmac_ba_rx_init")

    d = dis("hdpp_tx_tid_init")
    ck_regs(d, r"add\s+r5, r6, #1552", "tx init: user+0x610 base")
    ck_regs(d, r"add\s+r5, r5, #12", "tx init: 12-byte TX session stride")
    ck_regs(d, r"bl\s+db8e8 <hmac_ba_tx_init>", "tx init: 8x hmac_ba_tx_init")

    d = dis("hmac_ba_action_msg_process")
    ck_regs(d, r"ldrb\s+r4, \[r5, #15\]", "d2h 0x1d: action at hdr[15]")
    ck_regs(d, r"ldrb\s+r2, \[r5, #14\]", "d2h 0x1d: TID at hdr[14]")
    ck_regs(d, r"bl\s+af180 <hmac_tx_ba_setup>", "d2h 0x1d action 0: TX setup")
    ck_regs(d, r"bl\s+45a84 <hmac_mgmt_rx_addba_rsp>",
            "d2h 0x1d action 1: ADDBA rsp relay")
    ck_regs(d, r"add\s+r2, r5, #20", "d2h 0x1d action 1: frame at hdr+20")

    d = dis("hmac_create_ba_event")
    ck_regs(d, r"ldrb\s+r6, \[r5, #14\]", "d2h 2: TID at hdr[14]")
    ck_regs(d, r"ldrb\s+r8, \[r5, #15\]", "d2h 2: peer index at hdr[15]")
    ck_regs(d, r"bl\s+af180 <hmac_tx_ba_setup>", "d2h 2: TX BA setup")
    d = dis("hmac_del_ba_event")
    ck_regs(d, r"ldrb\s+r3, \[r5, #132\]",
            "d2h 3: TX session state user+tid*0x88+0x84")

    d = dis("hmac_config_ampdu_start")
    ck_regs(d, r"mov\s+r0, #0", "wal ampdu_start: NOP returns 0")
    ck_regs(d, r"bx\s+lr", "wal ampdu_start: plain return")
    d = dis("hmac_config_addba_req")
    ck_regs(d, r"bl\s+490e4 <hmac_mgmt_tx_action>",
            "wal addba_req: category-3 TX dispatcher")
    d = dis("hmac_config_set_baw_size")
    ck_regs(d, r"movw\s+r1, #3908", "wal set_baw: user+0xf44")
    ck_regs(d, r"movw\s+r0, #3910", "wal set_baw: user+0xf46")


def check_fw(fw, capstone_ok):
    data = open(fw, "rb").read()
    ck(len(data) == FW_LEN, "fw: pinned length %d" % FW_LEN)
    ck(hashlib.sha256(data).hexdigest() == FW_SHA256, "fw: pinned sha256")
    ck(len(data) > G0_TABLE - FW_BASE + 64, "fw: g0 table present")
    rows = {}
    for idx in range(0, 40):
        off = G0_TABLE - FW_BASE + idx * 12
        if off + 12 > len(data):
            break
        msgid, fn, aux = struct.unpack_from("<III", data, off)
        if msgid > 0x40:
            break
        rows[msgid] = (msgid, fn, aux)
    ck(rows.get(0x0a) == (0x0a, 0x5FF65, 0x0),
       "fw g0 id 0x0a -> CA 0x5ff64")
    ck(rows.get(0x0b) == (0x0b, 0x5E2F5, 0x0),
       "fw g0 id 0x0b -> CA 0x5e2f4")
    ck(rows.get(0x1c) == (0x1c, 0x5DFA1, 0x0),
       "fw g0 id 0x1c -> CA 0x5dfa0")
    if not capstone_ok:
        print("SKIP fw handler disassembly (capstone not importable)")
        return
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    def text(ca, n):
        out = []
        off = ca - FW_BASE
        for ins in md.disasm(data[off:off + n], ca):
            out.append("%s %s" % (ins.mnemonic, ins.op_str))
        return "\n".join(out)

    d = text(0x5E2F4, 0x150)
    ck("bl #0x961ae" in d, "fw id 0x0b handler: module lookup 0x961ae")
    ck("bl #0x95188" in d, "fw id 0x0b handler: aggregation ctx 0x95188")
    ck("bl #0xd6a1e" in d, "fw id 0x0b handler: object lookup 0xd6a1e")
    ck("ldrb r4, [r5, #0xc]" in d, "fw id 0x0b handler: subtype at hdr[12]")
    ck("cmp r4, #1" in d, "fw id 0x0b handler: subtype 0/1")

    d = text(0x5DFA0, 0x60)
    ck("movs r3, #0xb" in d, "fw id 0x1c handler: builds follow-up id 0x0b")
    ck("strb r3, [r0, #6]" in d, "fw id 0x1c handler: hdr[6] = id")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ko", required=True)
    ap.add_argument("--fw", required=True)
    ap.add_argument("--objdump", default=None)
    args = ap.parse_args()
    if not os.path.isfile(args.ko):
        sys.exit("error: --ko not found")
    if not os.path.isfile(args.fw):
        sys.exit("error: --fw not found")
    objdump = find_objdump(args.objdump)
    try:
        import capstone  # noqa: F401
        capstone_ok = True
    except Exception:
        capstone_ok = False
    ko = Elf(args.ko)
    print("# ba_decode_check - lane wifi-ba-agg (task-178)")
    print("# host module: %s (%d B)" % (os.path.basename(args.ko),
                                        len(ko.data)))
    print("# firmware:    %s" % os.path.basename(args.fw))
    print("# objdump:     %s" % objdump)
    print()
    check_symbols(ko)
    check_d2h_table(ko)
    check_wal_table(ko)
    check_host_disasm(ko, objdump)
    check_fw(args.fw, capstone_ok)
    print()
    print("SUMMARY: %d checks, %d failed" % (CHECKS[0], CHECKS[1]))
    return 0 if CHECKS[1] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
