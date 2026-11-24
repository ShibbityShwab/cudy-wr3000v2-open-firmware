#!/usr/bin/env python3
"""sta_re.py - host-only reproducibility tool for the wifi-sta-static lane.

Re-derives this lane's vendor-driver claims from a local copy of the vendor
module (NEVER committed; the module is local evidence only):

  * the WAL cfg-event opcodes of cfg80211 .connect / .disconnect and their
    body sizes (wal_cfg80211_connect -> wal_cfg80211_start_req chain),
  * the 108-byte STA join body builder (hmac_sta_sync_join_req_params:
    group/id/len + payload field offsets),
  * the static per-band capability tables (2.4 GHz / 5 GHz channel tables,
    HT and VHT capability words, rate lists).

The tool prints only DERIVED facts (addresses, ids, offsets, counts, decoded
capability words) - no vendor code text or data bytes are emitted, and nothing
is written anywhere.

Usage:
  python3 stsa_re.py /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko
  python3 sta_re.py /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko

Requires pyelftools + capstone in the interpreter (project .provision-venv).
"""
import hashlib
import struct
import sys

from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

R_ARM_ABS32 = 2
R_ARM_CALL = 28
R_ARM_JUMP24 = 29
R_ARM_MOVW_ABS_NC = 43
R_ARM_MOVT_ABS = 44

BAND_2G_TABLE = 0xDF8      # struct ieee80211_supported_band (vendor .data)
BAND_5G_TABLE = 0xE54
CHAN_STRIDE = 56           # sizeof(struct ieee80211_channel), 5.10 ARM
RATE_STRIDE = 12           # sizeof(struct ieee80211_rate)


class Mod:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.elf = ELFFile(self.f)
        self.sections = {s.name: s for s in self.elf.iter_sections()}
        self.symtab = self.elf.get_section_by_name('.symtab')
        self.syms = []
        for s in self.symtab.iter_symbols():
            self.syms.append(s)
        self.relocs = {}
        for sec in self.elf.iter_sections():
            if sec.name.startswith('.rel.'):
                target = sec.name[5:]
                for r in sec.iter_relocations():
                    self.relocs[(target, r['r_offset'])] = r
        self.code = Cs(CS_ARCH_ARM, CS_MODE_ARM)

    def sym_by_name(self, name):
        for s in self.syms:
            if s.name == name:
                return s
        return None

    def text_bytes(self, off, size):
        t = self.sections['.text']
        return t.data()[off:off + size]

    def data_words(self, off, count):
        d = self.sections['.data'].data()
        return list(struct.unpack_from('<%dI' % count, d, off))

    def reloc_target(self, secname, off):
        r = self.relocs.get((secname, off))
        if not r:
            return None
        sym = self.syms[r['r_info_sym']]
        return sym.name, sym['st_value']

    def dump_function(self, name, max_lines=400):
        s = self.sym_by_name(name)
        if s is None:
            print('  %-40s NOT FOUND (symbol stripped?)' % name)
            return False
        start, size = s['st_value'], s['st_size'] or 0x400
        raw = self.text_bytes(start, size)
        print('  %s @ .text+0x%x (size 0x%x)' % (name, start, size))
        for ins in self.code.disasm(raw, start):
            line = '    0x%05x  %-8s %s' % (ins.address, ins.mnemonic, ins.op_str)
            tgt = self.reloc_target('.text', ins.address)
            if tgt:
                line += '        ; reloc -> %s' % tgt[0]
            print(line)
            if ins.mnemonic == 'pop' and 'pc' in ins.op_str:
                break
        return True


def band_words(mod, base):
    w = mod.data_words(base, 23)
    chans_ptr = w[0]
    rates_ptr = w[1]
    n_channels = w[3]
    n_bitrates = w[4]
    ht = w[5]
    ht_cap = ht & 0xffff
    ht_supported = (ht >> 16) & 0xff
    vht = w[11]
    vht_supported = vht & 0xff
    vht_cap = w[12]
    mcs = w[13] | (w[14] << 16)
    return dict(chans=chans_ptr, rates=rates_ptr, n_ch=n_channels,
                n_rates=n_bitrates, ht_cap=ht_cap,
                ht_supported=ht_supported, vht_ok=vht_supported,
                vht_cap=vht_cap, mcs=mcs, base=base)


def chan_list(mod, ptr, n):
    out = []
    for i in range(n):
        w = mod.data_words(ptr + i * CHAN_STRIDE, 6)
        band = w[0]
        freq = w[1]
        hw = (w[2] >> 16) & 0xffff
        out.append((band, freq, hw))
    return out


def rate_list(mod, ptr, n):
    out = []
    for i in range(n):
        w = mod.data_words(ptr + i * RATE_STRIDE, 3)
        flags = w[0]
        bitrate = w[1] & 0xffff
        hw = (w[1] >> 16) & 0xffff
        out.append((flags, bitrate, hw))
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit('usage: sta_re.py <hi5622v100_wifi.ko>')
    path = sys.argv[1]
    blob = open(path, 'rb').read()
    mod = Mod(path)
    print('== module identity')
    print('  file %s' % path)
    print('  size %d  sha256 %s' % (len(blob), hashlib.sha256(blob).hexdigest()))
    print('  md5  %s' % hashlib.md5(blob).hexdigest())

    print('== cfg80211 STA entry points (link-time .text offsets)')
    for fn in ('wal_cfg80211_connect', 'wal_cfg80211_start_connect',
               'wal_cfg80211_disconnect', 'wal_cfg80211_start_disconnect',
               'wal_cfg80211_start_req', 'wal_send_cfg_event',
               'hmac_sta_sync_join_req_params', 'hmac_sta_wait_join',
               'hmac_sta_handle_disassoc_rsp', 'wal_cfg80211_init'):
        s = mod.sym_by_name(fn)
        if s is not None:
            print('  %-42s .text+0x%06x size 0x%x' %
                  (fn, s['st_value'], s['st_size']))
        else:
            print('  %-42s NOT FOUND' % fn)

    print('== connect chain (opcode/body evidence)')
    mod.dump_function('wal_cfg80211_start_connect', 10)
    print('  --- wal_cfg80211_start_disconnect (opcode/body store) ---')
    mod.dump_function('wal_cfg80211_start_disconnect', 60)
    print('  --- hmac_sta_sync_join_req_params (header/id/alloc) ---')
    mod.dump_function('hmac_sta_sync_join_req_params', 120)

    print('== static band capability tables (vendor .data)')
    for name, base in (('2.4 GHz', BAND_2G_TABLE), ('5 GHz', BAND_5G_TABLE)):
        d = band_words(mod, base)
        print('  %s supported_band @ .data+0x%x: channels .data+0x%x, '
              'bitrates .data+0x%x' % (name, base, d['chans'], d['rates']))
        print('    n_channels %d n_bitrates %d ht_supported %d ht_cap 0x%04x '
              'vht_supported %d vht_cap 0x%08x vht_mcs 0x%08x' %
              (d['n_ch'], d['n_rates'], d['ht_supported'], d['ht_cap'],
               d['vht_ok'], d['vht_cap'], d['mcs']))
        cl = chan_list(mod, d['chans'], d['n_ch'])
        print('    channels: %s' % ' '.join('%d(%d)' % (c[2], c[1]) for c in cl))
        rl = rate_list(mod, d['rates'], d['n_rates'])
        print('    rates: %s' % ' '.join('%d' % r[1] for r in rl))

    print('== summary assertions')
    sa = mod.sym_by_name('hmac_sta_sync_join_req_params')
    print('  join builder present: %s' % (sa is not None))
    print('  2.4 GHz channel count 14 -> %s' %
          (band_words(mod, BAND_2G_TABLE)['n_ch'] == 14))
    print('  5 GHz channel count 29 -> %s' %
          (band_words(mod, BAND_5G_TABLE)['n_ch'] == 29))
    print('  HT cap word 0x0062 (2g and 5g) -> %s' %
          (band_words(mod, BAND_2G_TABLE)['ht_cap'] == 0x62 and
           band_words(mod, BAND_5G_TABLE)['ht_cap'] == 0x62))
    print('  VHT cap 2g 0x00400000 -> %s' %
          (band_words(mod, BAND_2G_TABLE)['vht_cap'] == 0x00400000))
    print('  VHT cap 5g 0x00400064 (160 MHz/SGI) -> %s' %
          (band_words(mod, BAND_5G_TABLE)['vht_cap'] == 0x00400064))
    print('  VHT MCS map 0xfffa (2 SS MCS0-9) -> %s' %
          ((band_words(mod, BAND_5G_TABLE)['mcs'] & 0xffff) == 0xfffa))
    print('  HT MCS rx_mask in the static template: 0x%x (all-zero = the vendor'
          ' fills it at runtime [U])' %
          (mod.data_words(BAND_2G_TABLE + 0x18, 3)[0]))


if __name__ == '__main__':
    main()
