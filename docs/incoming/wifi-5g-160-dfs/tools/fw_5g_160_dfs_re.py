#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_5g_160_dfs_re.py - re-derive the 160 MHz + DFS decode from the pinned
local vendor artifacts (lane wifi-5g-160-dfs, task-205).

HOST-ONLY.  Reads the pinned local firmware image and vendor module IN PLACE;
prints derived values only (addresses, keys, channels, constants).  No vendor
binary/firmware byte is stored in this lane.  The window assertions run the
lab objdump on the artifacts; if it is missing the tool fails closed.

Usage:
  fw_5g_160_dfs_re.py --selftest
  fw_5g_160_dfs_re.py --tables
  fw_5g_160_dfs_re.py --frames
"""

import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

FW = '/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin'
KO = '/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko'
OD = ('/home/shibbityshwab/openwrt-lab/staging_dir/'
      'toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/'
      'arm-openwrt-linux-objdump')
FW_SHA = '7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b'
FW_SIZE = 928920
KO_SHA = '22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2'
KO_SIZE = 3581748
FW_CPU_BASE = 0x40000
KO_TEXT = 0x38
KO_RODATA = 0x167290

KEY_TABLE_A = 0x10C23C
KEY_ROWS = 217

EXPECTED_KEYS = {
    0x0124: (108, 0x841B8),
    0x0147: (100, 0x436BC),
    0x07E7: (94, 0xB58FC),
    0x0136: (95, 0xB5F3C),
    0x01F7: (93, 0xB5B9C),
    0x00F7: (92, 0x45A30),
}
FW_BAND0 = 0x117B48
FW_BAND1 = 0x117AD4
FW_CH5G = [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120,
           124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 184, 188,
           192, 196]
FW_CH2G = list(range(1, 15))

HOST_SYMBOLS = {
    'hmac_config_set_channel': 0x764E4,
    'hmac_chan_follow_reset_channel': 0x76D4C,
    'hmac_band_set_channel_and_bandwith': 0x13F520,
    'hmac_config_set_bw': 0x73A58,
    'hmac_config_cac_sync_state': 0xAD1D4,
    'hmac_dfs_init': 0xAC588,
    'hmac_dfs_channel_list_init': 0xAC600,
    'hmac_dfs_cac_start': 0xAD420,
    'hmac_dfs_cac_stop': 0xACD2C,
    'hmac_dfs_cac_timeout_fn': 0xAE584,
    'hmac_dfs_radar_detect_event': 0xAC7C0,
    'hmac_dfs_radar_wait': 0xAD7A4,
    'hmac_dfs_switch_channel_for_radar': 0xADBCC,
    'hmac_dfs_ap_up_radar_handler': 0xAE218,
    'hmac_dfs_nol_timeout_fn': 0xAB7FC,
    'hmac_dfs_off_chan_cac_timeout_fn': 0xAB034,
    'hmac_dfs_off_cac_stop': 0xAD0DC,
    'hmac_report_cac_start': 0xAD22C,
    'hmac_report_cac_finish': 0xACAEC,
    'hmac_cac_check_and_start_bss': 0x13FFA8,
    'hmac_check_set_channel_radar_status': 0x763D4,
    'hmac_config_dfs_radartool': 0x71CD8,
    'wal_cfg80211_set_chan_dfs_check': 0x102980,
    'wal_cfg80211_chan_dfs_check_resume': 0x102B44,
    'wal_cfg80211_convert_width_to_value': 0x101A98,
    'wal_cfg80211_convert_value_to_vht_width': 0x101328,
}

# (address, expected mnemonic, expected register operands prefix, ints that
# must appear anywhere in the operand list)
HOST_INSNS = [
    (0x73A68, 'movw', ['r1'], [327]),          # key 0x0147
    (0x76744, 'mov', ['r1'], [292]),           # key 0x0124
    (0x76E48, 'mov', ['r1'], [292]),           # key 0x0124 (follow-reset path)
    (0xAD478, 'movw', ['r1'], [2023]),         # key 0x07E7 (cac start)
    (0xAE684, 'movw', ['r1'], [2023]),         # key 0x07E7 (timeout finish)
    (0xAD1EC, 'movw', ['r1'], [2023]),         # key 0x07E7 (generic sync)
    (0x71DF8, 'movw', ['r1'], [310]),          # key 0x0136 radartool
    (0xAD55C, 'movw', ['r2'], [63000]),        # 63 s CAC tick
    (0xAE618, 'movw', ['r2'], [60000]),        # 60 s CAC re-arm
    (0xAD510, 'movw', ['r3'], [59999]),        # fast/slow CAC branch
    (0x13F52C, 'strb', ['r1'], [3369]),        # band+0xD29 = channel
    (0x13F530, 'strb', ['r2'], [210]),         # band+0xD2 -> band+0x10D2 = bw
    (0xAD460, 'strb', ['r3'], [3795]),         # band+0xED3 = cac band
    (0xAD474, 'strb', ['r1'], [3796]),         # band+0xED4 = cac channel
    (0xAC66C, 'cmp', ['r8'], [29]),            # 29 channel-list entries
    (0xAC6D4, 'sub', ['r2'], [5568]),          # TDWR window lower 5600
    (0xAC6D8, 'sub', ['r2'], [32]),
    (0xAC6E0, 'cmp', ['r2'], [50]),            # 5600..5650 window
    (0xAC58C, 'movw', ['ip'], [60000]),        # CAC default
    (0xAC5BC, 'mov', ['r3'], [10176]),         # 0x27C0
    (0xAC5C0, 'movt', ['r3'], [9]),            # 0x927C0 = 600000 ms TDWR CAC
    (0xAC5D4, 'movw', ['r3'], [61056]),        # 0xEE80
    (0xAC5D8, 'movt', ['r3'], [54]),           # 0x36EE80 = 3600000 ms
    (0xAC5DC, 'movw', ['r1'], [30528]),        # 0x7740
    (0xAC5E0, 'movt', ['r1'], [27]),           # 0x1B7740 = 1800000 ms NOL
    (0xAC5AC, 'movw', ['r1'], [32320]),        # 0x7E40
    (0xAC5B0, 'movt', ['r1'], [5]),            # 0x57E40 = 360000 ms
    (0x101328, 'cmp', ['r0'], [80]),           # vht width 80 -> 1
    (0x101330, 'cmp', ['r0'], [160]),          # vht width 160 -> 2
    (0x102F04, 'mov', ['r2'], [292]),          # DFS resume manual 0x0124
    (0x71DB4, 'ldr', ['r4'], [124]),           # radartool table handler offset
]

HOST_TABLES = [
    # (description, file offset, expected u32 list)
    ('width->value table (NL80211_CHAN_WIDTH 0..5)',
     KO_RODATA + 0x9390, [20, 20, 40, 80, 80, 160]),
]

FW_INSNS = [
    (0x436BC, 'push', ['r3', 'r4', 'r5', 'lr'], []),
    (0x436EA, 'strb.w', ['r3'], [1329]),        # obj+0x531 = body[0] bw byte
    (0xB5900, 'ldrb', ['r3'], [17]),            # worker state byte [ctx+17]
    (0xB5F46, 'add', ['r2', 'sp'], [8]),        # radartool string parse
    (0x101894, 'cmp', ['r0'], [1]),             # band-1-only predicate
]


class Checks:
    def __init__(self):
        self.pass_n = 0
        self.fail = []

    def check(self, name, ok, detail=''):
        if ok:
            self.pass_n += 1
            print('PASS %s' % name)
        else:
            self.fail.append(name)
            print('FAIL %s %s' % (name, detail))


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def cpu_off(ca):
    return ca - FW_CPU_BASE


class Firmware:
    def __init__(self, data):
        self.d = data

    def u8(self, ca):
        return self.d[cpu_off(ca)]

    def u16(self, ca):
        return struct.unpack_from('<H', self.d, cpu_off(ca))[0]

    def u32(self, ca):
        return struct.unpack_from('<I', self.d, cpu_off(ca))[0]

    def key_table(self):
        rows = []
        for i in range(KEY_ROWS):
            k = self.u16(KEY_TABLE_A + 8 * i)
            pad = self.u16(KEY_TABLE_A + 8 * i + 2)
            w = self.u32(KEY_TABLE_A + 8 * i + 4)
            rows.append((i, k, pad, w))
        return rows

    def lookup(self, key):
        for i, k, _pad, w in self.key_table():
            if k == key:
                return i, w
        return None

    def chans(self, ca, count):
        out = []
        for i in range(count):
            w = self.u32(ca + 4 * i)
            out.append((w & 0xFF, (w >> 8) & 0xFF))
        return out


class Disasm:
    def __init__(self, text):
        self.insns = {}
        for line in text.splitlines():
            m = re.match(r'^\s*([0-9a-f]+):\s*(.*)$', line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            rest = m.group(2).split('@')[0].strip()
            if '\t' in rest:
                # objdump separates the machine-code field from the asm by a tab
                rest = rest.split('\t', 1)[1]
            asm = re.sub(r'\s+', ' ', rest).strip()
            if not asm:
                continue
            self.insns[addr] = asm

    def insn(self, addr):
        return self.insns.get(addr)


def run_objdump(args):
    return subprocess.run([OD] + args, capture_output=True, text=True,
                          check=False).stdout


def parse_symbols(text):
    syms = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 5:
            continue
        if not re.match(r'^[0-9a-f]{6,8}$', parts[0]):
            continue
        name = parts[-1]
        if name.startswith('0x') or name.startswith('.'):
            continue
        syms[name] = int(parts[0], 16)
    return syms


def get_ints(asm):
    ints = []
    for m in re.finditer(r'#(0x[0-9a-fA-F]+|\d+)', asm):
        ints.append(int(m.group(1), 0))
    return ints


def check_insn(ck, dis, addr, mnem, regs, ints, label):
    asm = dis.insn(addr)
    if asm is None:
        ck.check(label, False, 'no instruction at 0x%x' % addr)
        return
    toks = (asm.replace(',', ' ').replace('[', ' ').replace(']', ' ')
            .replace('{', ' ').replace('}', ' ').split())
    ok = toks[0] == mnem
    for r in regs:
        if r not in toks:
            ok = False
    got = get_ints(asm)
    for i in ints:
        if i not in got:
            ok = False
    ck.check(label, ok, 'got "%s"' % asm)


def main():
    global OD
    ap = argparse.ArgumentParser()
    ap.add_argument('--fw', default=FW)
    ap.add_argument('--ko', default=KO)
    ap.add_argument('--objdump', default=OD)
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--tables', action='store_true')
    ap.add_argument('--frames', action='store_true')
    args = ap.parse_args()
    OD = args.objdump

    ck = Checks()
    fw_data = read(args.fw)
    ko_data = read(args.ko)
    fw = Firmware(fw_data)

    ck.check('firmware size 928920', len(fw_data) == FW_SIZE,
             str(len(fw_data)))
    ck.check('firmware sha256 pinned', sha256(fw_data) == FW_SHA)
    ck.check('host module size 3581748', len(ko_data) == KO_SIZE,
             str(len(ko_data)))
    ck.check('host module sha256 pinned', sha256(ko_data) == KO_SHA)

    # Firmware key table: key -> table A row/worker.
    for key, (row, worker) in sorted(EXPECTED_KEYS.items()):
        got = fw.lookup(key)
        ok = (got is not None and got[0] == row and
              (got[1] & ~1) == worker and (got[1] & 1) == 1)
        ck.check('fw key 0x%04X -> row %d worker 0x%X' % (key, row, worker),
                 ok, 'got %r' % (got,))

    # Firmware channel tables.
    ch5 = fw.chans(FW_BAND1, 29)
    ck.check('fw band-1 29 channels',
             [c for c, _f in ch5] == FW_CH5G, str([c for c, _f in ch5]))
    ck.check('fw band-1 flag byte 0xFF', all(f == 0xFF for _c, f in ch5))
    ch2 = fw.chans(FW_BAND0, 14)
    ck.check('fw band-0 14 channels',
             [c for c, _f in ch2] == FW_CH2G)
    dfs = [c for c, _f in ch5 if c in (52, 56, 60, 64) or 100 <= c <= 144]
    ck.check('fw init-able UNII-2/2e = 16 channels (52-64, 100-144)',
             len(dfs) == 16, str(dfs))

    # Firmware RF descriptor table pointer (literal at CA 0x101BA0).
    ck.check('fw CA 0x101BA0 literal = CA 0x10BE68 (RF descriptor table)',
             fw.u32(0x101BA0) == 0x10BE68, hex(fw.u32(0x101BA0)))

    if not os.path.exists(OD):
        print('FAIL lab objdump missing: %s (window assertions fail closed)'
              % OD)
        sys.exit(2)

    fw_ranges = [(0x436B8, 0x43700), (0xB58F8, 0xB5914),
                 (0xB5F38, 0xB5F60), (0x10188C, 0x1018E0)]
    fw_dis = Disasm('')
    for start, stop in fw_ranges:
        text = run_objdump(['-D', '-b', 'binary', '-m', 'arm',
                            '-M', 'force-thumb',
                            '--adjust-vma=0x40000',
                            '--start-address=0x%x' % start,
                            '--stop-address=0x%x' % stop, args.fw])
        fw_dis.insns.update(Disasm(text).insns)
    for addr, mnem, regs, ints in FW_INSNS:
        check_insn(ck, fw_dis, addr, mnem, regs, ints,
                   'fw insn 0x%X %s' % (addr, mnem))

    ko_syml = run_objdump(['-t', args.ko])
    ko_syms = parse_symbols(ko_syml)
    for name, addr in sorted(HOST_SYMBOLS.items()):
        ck.check('ko symbol %s = 0x%X' % (name, addr),
                 ko_syms.get(name) == addr,
                 'got %r' % (ko_syms.get(name),))

    ko_dis_text = run_objdump(['-d', args.ko])
    ko_dis = Disasm(ko_dis_text)
    for addr, mnem, regs, ints in HOST_INSNS:
        check_insn(ck, ko_dis, addr, mnem, regs, ints,
                   'ko insn 0x%X %s' % (addr, mnem))
    asm = ko_dis.insn(0xAD480) or ''
    ck.check('ko 0xAD480 calls hmac_config_send_event',
             'hmac_config_send_event' in asm, asm)

    # Host .rodata tables.
    for desc, foff, expected in HOST_TABLES:
        got = list(struct.unpack_from('<%dI' % len(expected), ko_data, foff))
        ck.check('ko table: %s' % desc, got == expected, str(got))

    # DFS timing semantics derived from the immediates.
    def val(lo, hi_shift):
        w_lo = fw_dis.insn(0xAC5BC) if lo == 0xAC5BC else None
        return w_lo
    ck.check('DFS CAC default 60000 ms',
             60000 in get_ints(ko_dis.insn(0xAC58C) or ''))
    ck.check('DFS TDWR CAC 600000 ms (0x27C0 | 9<<16)',
             (10176 | (9 << 16)) == 600000)
    ck.check('DFS NOL 1800000 ms (0x7740 | 27<<16)',
             (30528 | (27 << 16)) == 1800000)
    ck.check('DFS 60 min field 3600000 ms (0xEE80 | 54<<16)',
             (61056 | (54 << 16)) == 3600000)
    ck.check('DFS 6 min field 360000 ms (0x7E40 | 5<<16)',
             (32320 | (5 << 16)) == 360000)

    print('')
    print('SELFTEST: %d PASS, %d FAIL' % (ck.pass_n, len(ck.fail)))
    if ck.fail:
        for f in ck.fail:
            print('  FAILED: %s' % f)
        sys.exit(1)

    if args.tables:
        print('')
        print('== firmware key table A rows (CA 0x10C23C) for this lane ==')
        for key, (row, worker) in sorted(EXPECTED_KEYS.items()):
            print('  key 0x%04X row %3d worker CA 0x%06X' % (key, row, worker))
        print('== firmware band-1 channel table (CA 0x117AD4, 29) ==')
        print('  ' + ' '.join('%d' % c for c, _f in fw.chans(FW_BAND1, 29)))
        print('== firmware band-0 channel table (CA 0x117B48, 14) ==')
        print('  ' + ' '.join('%d' % c for c, _f in fw.chans(FW_BAND0, 14)))
        print('== DFS timing constants (host hmac_dfs_init 0xAC588) ==')
        print('  CAC default 60000 ms; TDWR window 5600-5650 CAC 600000 ms;')
        print('  NOL 1800000 ms; extra 3600000 ms / 360000 ms;')
        print('  tick 63000 ms (slow) or 60000 ms (fast); branch at 59999.')
        print('== host 5 GHz/VHT and width symbols ==')
        for name, addr in sorted(HOST_SYMBOLS.items()):
            print('  0x%06X %s' % (addr, name))
        print('== width->MHz table (host .rodata file 0x%06X) =='
              % (KO_RODATA + 0x9390))
        print('  NL80211_CHAN_WIDTH 0..5 -> 20,20,40,80,80,160 (5=80+80 '
              'falls back to 80)')
    if args.frames:
        print('')
        print('== H2D g0 id1 config event frames (derived layout) ==')
        print('  0x0124 channel config, len 4:')
        print('    body[0] = band (0=2.4G, 1=5G)')
        print('    body[1] = bandwidth selector (0=20M, 1=40M, 2=80M,')
        print('              3=160M [I]; the 0x0124 worker stores it to')
        print('              module ctx+0x12; the 5 GHz RF branch CA 0x40684')
        print('              passes it as field to CA 0x101890)')
        print('    body[2] = channel number (firmware band table)')
        print('    body[3] = 0')
        print('  0x07E7 CAC/radar state sync, len 1:')
        print('    body[0] = 1 arm radar detect + CAC active, 0 disarm/finish')
        print('  0x0136 radartool, len = strlen: the radartool command STRING')
        print('    (firmware 28-entry command table CA 0x105B68)')
        print('  0x0147 set bandwidth, len 1:')
        print('    body[0] = MAC bandwidth byte -> firmware obj+0x531')
        print('    (WAL set_bw/hipriv path; not the AP width path)')

    return 0


if __name__ == '__main__':
    sys.exit(main())
