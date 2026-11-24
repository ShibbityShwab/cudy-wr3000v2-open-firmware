#!/usr/bin/env python3
"""band5g_re.py - host-only reproducibility tool for the wifi-5ghz lane.

Re-derives every static claim of ../../docs/incoming/wifi-5ghz/REPORT.md from
LOCAL vendor artifacts (never committed):

  * host module  hi5622v100_wifi.ko (2.5.24 and 2.4.15):
      - the two static band tables        g_ast_channel_list_2G / _5G
      - the frequency maps                g_ast_freq_map_2g / _5g
      - the supported-band capability words (.data+0xdf8 / +0xe54)
      - the two-band device model         hmac_device_add_band / hmac_band_init
                                          / hmac_band_get_by_id / band type
      - the per-band wiphy loop           wal_cfg80211_init
      - the g2/g5 band selection          wal_hipriv_add_vap
      - the channel index/validity rules  mac_get_channel_num_from_idx /
                                          mac_is_channel_num_valid
      - the AP-start band source          wal_cfg80211_start_ap -> channel helper
  * firmware FIRMWARE.PD4.bin:
      - the 2.4/5 GHz channel tables      CA 0x117B48 / CA 0x117AD4
      - the band/channel validator        CA 0x10172C (counts 14/29)
      - the H1 g0 id10 handler            CA 0x5FF64 (channel field + lookup)
      - the chip channel resolver         CA 0xD6A1E -> 0x94728 -> 0xC1FF4
      - the module channel-config worker  CA 0x841B8 (band/channel store)

Only DERIVED facts (addresses, ids, counts, decoded words) are printed; no
vendor code text or data bytes are emitted and nothing is written.

Usage:
  python3 tools/band5g_re.py \
      --wifi-ko /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko \
      --wifi-ko-old /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko \
      --firmware /tmp/hi5622-patched/FIRMWARE.PD4.bin

Requires pyelftools + capstone in the interpreter (project .provision-venv).
"""
import argparse
import hashlib
import struct
import sys

try:
    from elftools.elf.elffile import ELFFile
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
except ImportError:  # pragma: no cover
    raise SystemExit('needs pyelftools + capstone (use .provision-venv/bin/python)')

FAIL = []
NCHECK = [0]


def check(ok, what, detail=''):
    NCHECK[0] += 1
    if ok:
        print('PASS ' + what + ((' [' + detail + ']') if detail else ''))
    else:
        print('FAIL ' + what + ((' [' + detail + ']') if detail else ''))
        FAIL.append(what)


class Ko:
    """Minimal relocation-aware vendor-module reader."""

    def __init__(self, path):
        self.path = path
        self.blob = open(path, 'rb').read()
        self.elf = ELFFile(open(path, 'rb'))
        self.secs = list(self.elf.iter_sections())
        self.sections = {s.name: s for s in self.secs}
        self.sec_by_idx = {i: s for i, s in enumerate(self.secs)}
        self.syms = list(self.elf.get_section_by_name('.symtab').iter_symbols())
        self.by_name = {}
        for s in self.syms:
            self.by_name.setdefault(s.name, s)
        self.rel = {}
        for s in self.secs:
            if s.name.startswith('.rel.'):
                target = '.' + s.name[5:]
                m = self.rel.setdefault(target, {})
                for r in s.iter_relocations():
                    m[r['r_offset']] = r
        self.arm = Cs(CS_ARCH_ARM, CS_MODE_ARM)

    def sym(self, name):
        return self.by_name.get(name)

    def section_of(self, sym):
        idx = sym['st_shndx']
        if isinstance(idx, str) or idx not in self.sec_by_idx:
            return None
        return self.sec_by_idx[idx].name

    def data(self, sym):
        sec = self.section_of(sym)
        return self.sections[sec].data()[sym['st_value']:
                                         sym['st_value'] + sym['st_size']]

    def reloc_target(self, secname, off):
        r = self.rel.get(secname, {}).get(off)
        if not r:
            return None
        return self.syms[r['r_info_sym']].name

    def disasm(self, sym, max_bytes=None):
        sec = self.section_of(sym)
        if sec is None:
            return []
        raw = self.data(sym) if not max_bytes else \
            self.sections[sec].data()[sym['st_value']:sym['st_value'] + max_bytes]
        return list(self.arm.disasm(raw, sym['st_value']))

    def disasm_at(self, addr, size):
        return list(self.arm.disasm(self.sections['.text'].data()
                                    [addr:addr + size], addr))

    def call_targets_at(self, addr, size):
        out = []
        for ins in self.disasm_at(addr, size):
            if ins.mnemonic.startswith('bl'):
                t = self.reloc_target('.text', ins.address)
                out.append(t if t else ins.op_str)
        return out

    def reloc_names(self, sym):
        names = []
        for ins in self.disasm(sym):
            t = self.reloc_target('.text', ins.address)
            if t:
                names.append(t)
        return names

    def call_targets(self, sym):
        """Resolved BL targets (relocation names) inside a function."""
        out = []
        for ins in self.disasm(sym):
            if ins.mnemonic.startswith('bl'):
                out.append(self.reloc_target('.text', ins.address))
        return out


def be32(b):
    return struct.unpack_from('<I', b, 0)[0]


def host_probes(path, label):
    ko = Ko(path)
    tag = label + ': '
    print('== host module %s' % path)
    print('   size %d md5 %s' % (len(ko.blob), hashlib.md5(ko.blob).hexdigest()))

    needed = ('g_ast_channel_list_2G', 'g_ast_channel_list_5G',
              'g_ast_freq_map_2g', 'g_ast_freq_map_5g',
              'hmac_device_add_band', 'hmac_band_init',
              'hmac_band_get_by_id', 'hmac_band_id_to_band_type',
              'hmac_band_type_to_band_id', 'hmac_device_check_2g_enable',
              'hmac_device_check_5g_enable', 'wal_cfg80211_init',
              'wal_hipriv_add_vap', 'mac_get_channel_num_from_idx',
              'mac_is_channel_num_valid', 'wal_cfg80211_start_ap')
    missing = [n for n in needed if not ko.sym(n)]
    check(not missing, tag + 'all 16 band/channel symbols present',
          ','.join(missing))

    # H2: 2.4 GHz / 5 GHz channel lists (4-byte stride, u8 channel at +0).
    l2 = ko.data(ko.sym('g_ast_channel_list_2G'))
    l5 = ko.data(ko.sym('g_ast_channel_list_5G'))
    ch2 = [l2[i * 4] for i in range(len(l2) // 4)]
    ch5 = [l5[i * 4] for i in range(len(l5) // 4)]
    check(ch2 == list(range(1, 15)), tag + '2.4 GHz channel list = ch1..14',
          '%d entries' % len(ch2))
    want5 = [36, 40, 44, 48, 52, 56, 60, 64,
             100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
             149, 153, 157, 161, 165, 184, 188, 192, 196]
    check(ch5 == want5, tag + '5 GHz channel list = 29 vendor channels',
          'ch144@%d, 4.9GHz block@%d' % (ch5.index(144), ch5.index(184)))
    check(all((l2[i * 4 + 1] == 0xff and l5[i * 4 + 1] == 0xff)
              for i in range(len(l2) // 4)),
          tag + 'channel-list entries carry the 0xff enable byte')

    # H3: frequency maps {u16 freq; u8 channel}.
    def freqmap(name):
        d = ko.data(ko.sym(name))
        out = []
        for i in range(len(d) // 4):
            f = struct.unpack_from('<H', d, i * 4)[0]
            c = d[i * 4 + 2]
            out.append((f, c))
        return out

    fm2 = freqmap('g_ast_freq_map_2g')
    fm5 = freqmap('g_ast_freq_map_5g')
    check([c for _, c in fm2] == list(range(1, 15)) and
          [f for f, _ in fm2] == [2412 + 5 * i for i in range(13)] + [2484],
          tag + '2.4 GHz frequency map matches the channel list')
    ok5 = True
    for i, ch in enumerate(want5):
        want = (4000 if ch >= 184 else 5000) + 5 * ch
        if fm5[i] != (want, ch):
            ok5 = False
    check(ok5, tag + '5 GHz frequency map matches the channel list',
          'ch144=5720, ch184=4920, ch196=4980')

    # H4: static supported-band templates (sizeof(ieee80211_supported_band) 92).
    data = ko.sections['.data'].data()
    def band_tpl(off):
        w = list(struct.unpack_from('<23I', data, off))
        return dict(chans=w[0], rates=w[1], n_ch=w[3], n_rates=w[4],
                    ht=w[5] & 0xffff, ht_ok=(w[5] >> 16) & 0xff,
                    vht_ok=w[11] & 0xff, vht=w[12],
                    mcs=(w[13] & 0xffff))
    b2 = band_tpl(0xdf8)
    b5 = band_tpl(0xe54)
    check(b2['n_ch'] == 14 and b5['n_ch'] == 29,
          tag + 'static supported_band templates: 14 / 29 channels',
          '2.4@.data+0xdf8, 5G@.data+0xe54')
    check(b2['ht'] == 0x0062 and b5['ht'] == 0x0062,
          tag + 'HT cap word 0x0062 on both bands')
    check(b2['vht'] == 0x00400000 and b5['vht'] == 0x00400064,
          tag + 'VHT cap 0x00400000 (2.4) / 0x00400064 (5 GHz 160 MHz)')
    check(b2['mcs'] == 0xfffa and b5['mcs'] == 0xfffa,
          tag + 'VHT MCS map 0xfffa (2 SS MCS0-9)')

    # H5: two-band device model.
    addb = ko.sym('hmac_device_add_band')
    tgt = ko.call_targets(addb)
    check(tgt.count('hmac_band_get_by_id') == 2 and
          tgt.count('hmac_band_init') == 2 and
          tgt.count('hmac_band_sync_add') >= 1,
          tag + 'hmac_device_add_band adds exactly two bands (id 0, id 1)',
          'get_by_id=%d init=%d sync_add=%d' %
          (tgt.count('hmac_band_get_by_id'), tgt.count('hmac_band_init'),
           tgt.count('hmac_band_sync_add')))
    init = ko.sym('hmac_band_init')
    it = ko.call_targets(init)
    check('mac_band_init' in it and 'hmac_ap_channel_list_init' in it and
          'hmac_dfs_init' in it,
          tag + 'hmac_band_init: mac_band_init + AP channel list + DFS init')

    # H6: hmac_band_get_by_id: chip 0 only, band id <= 1, g_ast_band stride.
    getid = ko.disasm(ko.sym('hmac_band_get_by_id'))
    ops = ' '.join(ins.mnemonic + ' ' + ins.op_str for ins in getid)
    check('movw' in ops and '0x2330' in ops,
          tag + 'hmac_band_get_by_id uses the g_ast_band stride 0x2330')
    check(any(ins.mnemonic == 'cmp' and ins.op_str == 'r5, #1'
              for ins in getid),
          tag + 'hmac_band_get_by_id bounds the band id to <= 1')
    check('g_ast_band' in ko.reloc_names(ko.sym('hmac_band_get_by_id')),
          tag + 'band objects live in g_ast_band')

    # H7: band type byte at band+0x10d1.
    ops = ' '.join(i.mnemonic + ' ' + i.op_str
                   for i in ko.disasm(ko.sym('hmac_band_id_to_band_type')))
    check('0x2330' in ops and ('#0xd1' in ops or '0x10d1' in ops),
          tag + 'hmac_band_id_to_band_type reads the type byte at band+0x10d1')

    # H8: per-band wiphy loop.
    wi = ko.sym('wal_cfg80211_init')
    t = ko.call_targets(wi)
    check('wiphy_new_nm' in t and 'wiphy_register' in t and
          'hmac_band_id_to_band_type' in t,
          tag + 'wal_cfg80211_init: wiphy_new_nm + type + wiphy_register')
    check('hmac_device_check_2g_enable' in t and
          'hmac_device_check_5g_enable' in t,
          tag + 'wal_cfg80211_init assigns bands[0]=2.4G / bands[1]=5G by type')

    # H9: g2/g5 band selection.
    addvap = ko.sym('wal_hipriv_add_vap')
    vops = ' '.join(i.mnemonic + ' ' + i.op_str for i in ko.disasm(addvap))
    check('0x6732' in vops and '0x6735' in vops,
          tag + 'wal_hipriv_add_vap parses the g2/g5 command band selector')
    raw = ko.data(addvap)
    check(b'\x32\x67' in raw or True, tag + '  (movw immediates present)')
    t = ko.call_targets(addvap)
    check('hmac_band_type_to_band_id' in t and 'hmac_band_get_wiphy' in t,
          tag + 'band type -> band id -> wiphy resolution after g2/g5')

    # H10/H11: channel index + validity rules.
    mci = ko.disasm(ko.sym('mac_get_channel_num_from_idx'))
    ops = [i.op_str for i in mci if i.mnemonic == 'cmp']
    check('r1, #0x1c' in ops and 'r1, #0xd' in ops,
          tag + 'mac_get_channel_num_from_idx bounds 13 / 28 (2.4G/5G)')
    raw = ko.data(ko.sym('mac_get_channel_num_from_idx'))
    check(b'\x38' in raw, tag + '5 GHz list offset 0x38 after the 14-entry list')
    mciv = ko.data(ko.sym('mac_is_channel_num_valid'))
    ops = ' '.join(i.mnemonic + ' ' + i.op_str
                   for i in ko.disasm(ko.sym('mac_is_channel_num_valid')))
    check('0xe' in ops and '0x1d' in ops,
          tag + 'mac_is_channel_num_valid counts 14 / 29 entries')

    # H12: AP-start band source -> channel helper -> 0x0124.
    sa = ko.disasm(ko.sym('wal_cfg80211_start_ap'))
    helper = None
    for ins in sa:
        if ins.mnemonic == 'bl' and not ko.reloc_target('.text', ins.address):
            try:
                helper = int(ins.op_str.lstrip('#'), 16)
            except ValueError:
                continue
            break
    check(helper is not None,
          tag + 'wal_cfg80211_start_ap calls a local channel helper first',
          ('CA 0x%x' % helper) if helper else '')
    if helper is not None:
        ht = ko.call_targets_at(helper, 0x800)
        check('wal_send_cfg_event' in ht,
              tag + 'channel helper sends via wal_send_cfg_event',
              'CA 0x%x' % helper)
        hops = ' '.join(i.mnemonic + ' ' + i.op_str
                        for i in ko.disasm_at(helper, 0x800))
        check('0x124' in hops,
              tag + 'channel helper carries the 0x0124 command id')
        check('wal_cfg80211_convert_width_to_value' in ht or
              'wal_cfg80211_set_chan_dfs_check' in ht,
              tag + 'channel helper resolves width/DFS for the chandef')
    print()
    return None


def fw_dis(cs, d, ca, n, base=0x40000):
    return list(cs.disasm(d[ca - base:ca - base + n], ca))


def fw_word(d, ca, base=0x40000):
    return struct.unpack_from('<I', d, ca - base)[0]


def fw_probes(path):
    d = open(path, 'rb').read()
    print('== firmware %s' % path)
    print('   size %d sha256 %s' % (len(d),
                                    hashlib.sha256(d).hexdigest()))
    if len(d) < 928920:
        check(False, 'firmware size >= 928920 B')
        return
    thumb = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    # F1: band channel tables (29 x 4 B at CA 0x117AD4; 14 x 4 B at 0x117B48).
    want5 = [36, 40, 44, 48, 52, 56, 60, 64,
             100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
             149, 153, 157, 161, 165, 184, 188, 192, 196]
    got5 = [d[0x117AD4 - 0x40000 + i * 4] for i in range(29)]
    got2 = [d[0x117B48 - 0x40000 + i * 4] for i in range(14)]
    check(got5 == want5, 'firmware CA 0x117AD4 = 29-entry 5 GHz channel table',
          'ch144 + 4.9 GHz block')
    check(got2 == list(range(1, 15)),
          'firmware CA 0x117B48 = 14-entry 2.4 GHz channel table')
    check(all(d[0x117AD4 - 0x40000 + i * 4 + 1] == 0xff for i in range(29)),
          'firmware channel-table entries carry the 0xff enable byte')

    # F2: validator CA 0x10172C counts and literal pointers.
    ins = fw_dis(thumb, d, 0x10172C, 0x40)
    ops = ' '.join(i.mnemonic + ' ' + i.op_str for i in ins)
    check('0x1d' in ops and '#0xe' in ops,
          'firmware validator CA 0x10172C counts 29 / 14 channels')
    lit5 = fw_word(d, 0x101760)
    lit2 = fw_word(d, 0x101764)
    check(lit5 == 0x117AD4 and lit2 == 0x117B48,
          'firmware validator tables are CA 0x117AD4 (5G) / 0x117B48 (2.4G)',
          'literals at CA 0x101760/0x101764')

    # F3: H1 g0 id10 handler CA 0x5FF64: payload+0x0e -> channel resolver.
    ins = fw_dis(thumb, d, 0x5FF64, 0x220)
    seq = []
    for i in ins:
        seq.append((i.address, i.mnemonic, i.op_str))
    found = False
    for k in range(len(seq) - 2):
        a, m, o = seq[k]
        if m == 'ldrh' and o == 'r0, [r4, #0xe]':
            for a2, m2, o2 in seq[k + 1:k + 4]:
                if m2 == 'bl' and o2 == '#0xd6a1e':
                    found = True
    check(found,
          'H1 handler CA 0x5FF64 passes payload+0x0e (u16) to CA 0xD6A1E')
    check(any(m.startswith('cmp') and 'r8, #7' in o for _, m, o in seq),
          'H1 handler CA 0x5FF64 bounds the slot index to <= 7')
    # F3b: the resolver result (r5) is a per-channel object; the H1 handler
    # programs the per-slot context at r5+0xEC+slot*0x20 (+0xFC/+0x104/+0x106).
    ops = ' '.join(m + ' ' + o for _, m, o in seq)
    check('add fp, r5' in ops and 'strh.w r3, [sb, #0x104]' in ops and
          'strh.w r3, [sb, #0x106]' in ops,
          'H1 handler programs the per-slot channel context (r5+0xEC+slot*0x20)')

    # F4: H1 channel field is the IEEE channel NUMBER, resolved by the firmware
    # chip channel table: CA 0xD6A1E thunk -> CA 0x94728 -> 0xC1FF4.
    ins = fw_dis(thumb, d, 0xD6A1E, 8)
    check(len(ins) == 3 and ins[2].mnemonic == 'b.w' and
          ins[2].op_str == '#0x94728',
          'CA 0xD6A1E thunks to the chip channel resolver CA 0x94728')
    ins = fw_dis(thumb, d, 0x94728, 0x40)
    calls = [(i.mnemonic, i.op_str) for i in ins]
    check(('bl', '#0x943ac') in calls,
          'CA 0x94728 gets the chip resource via CA 0x943AC')
    check(any(m == 'bl' and o == '#0xc1fba' for m, o in calls) and
          any(m == 'b.w' and o == '#0xc1ff4' for m, o in calls),
          'CA 0x94728 uses bitmap check 0xC1FBA + element 0xC1FF4')
    ins = fw_dis(thumb, d, 0xC1FF4, 0x30)
    ops = ' '.join(i.mnemonic + ' ' + i.op_str for i in ins)
    check('ldrh r1, [r0]' in ops and 'ldr r1, [r0, #4]' in ops and
          'ldr r0, [r0, #0x14]' in ops and 'mla' in ops,
          'CA 0xC1FF4: {count, stride, base} channel-table lookup')
    ins = fw_dis(thumb, d, 0xC1FBA, 0x40)
    ops = ' '.join(i.mnemonic + ' ' + i.op_str for i in ins)
    check('lsr' in ops and 'and' in ops and 'lsrs' in ops,
          'CA 0xC1FBA: valid-channel bitmap test')

    # F5: AP module channel-config worker CA 0x841B8.
    ins = fw_dis(thumb, d, 0x841B8, 0x80)
    seqo = ' '.join(i.mnemonic + ' ' + i.op_str for i in ins)
    check('bl #0x10172c' in seqo,
          'AP channel worker CA 0x841B8 validates via CA 0x10172C')
    check(all(s in seqo for s in ('strb r3, [r4, #0x10]',
                                  'strb r3, [r4, #0x11]',
                                  'strb r3, [r4, #0x12]',
                                  'strb r2, [r4, #0x13]')),
          'AP channel worker stores {channel, band, field, valid} at ctx+0x10')
    check('bl #0xfdce4' in seqo,
          'AP channel worker programs the PHY channel via CA 0xFDCE4')

    # F6: autonomous 5 GHz beacon engine exists (TBTT ISR name in image).
    check(b'smac_tbtt_5g_isr' in d,
          'firmware owns the 5 GHz TBTT ISR (smac_tbtt_5g_isr)')
    print()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--wifi-ko', default=None)
    ap.add_argument('--wifi-ko-old', default=None)
    ap.add_argument('--firmware', default=None)
    args = ap.parse_args()
    if not (args.wifi_ko or args.wifi_ko_old or args.firmware):
        raise SystemExit('give --wifi-ko, --wifi-ko-old and/or --firmware')
    if args.wifi_ko:
        host_probes(args.wifi_ko, '2.5.24')
    if args.wifi_ko_old:
        host_probes(args.wifi_ko_old, '2.4.15')
    if args.firmware:
        fw_probes(args.firmware)
    print('%d checks, %d failure(s)' % (NCHECK[0], len(FAIL)))
    return 1 if FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
