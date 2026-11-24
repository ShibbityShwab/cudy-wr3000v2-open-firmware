#!/usr/bin/env python3
"""sta_ops_re.py - host-only reproducibility tool for the wifi-sta-ops lane
(shared task task-149).

Re-derives this lane's vendor-driver and PD4-firmware claims from LOCAL
artifacts that are never committed (the vendor host module and the local PD4
research firmware image).  It prints only DERIVED facts (function offsets,
write offsets, command ids, dispatch-table rows, worker semantics) - no
vendor code text, no firmware bytes and no per-unit identity is emitted, and
nothing is written anywhere.

Use the project interpreter (.provision-venv/bin/python: pyelftools +
capstone).  Exit status 0 = all derived facts match hi5622_sta_ops_logic.h.
"""
import hashlib
import struct
import sys

from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB

CA_BIAS = 0x40000            # firmware CA = image file offset + 0x40000
TABLE_A = 0x10C23C           # config-event dispatch {u16 key;u16 pad;u32 worker}
CFG_CMD_ASOC_SYN = 0x07D2

failures = []


def check(cond, what):
    if cond:
        print("  ok   " + what)
    else:
        print("  FAIL " + what)
        failures.append(what)


class VendorKo:
    def __init__(self, path):
        self.path = path
        self.f = open(path, 'rb')
        self.raw = self.f.read()
        self.f.seek(0)
        self.elf = ELFFile(self.f)
        self.symtab = self.elf.get_section_by_name('.symtab')
        self.syms = {}
        self.by_addr = []
        for s in self.symtab.iter_symbols():
            if s['st_info']['type'] == 'STT_FUNC' and s['st_value']:
                self.syms[s.name] = (s['st_value'], s['st_size'])
                self.by_addr.append((s['st_value'], s.name, s['st_size']))
        self.by_addr.sort()
        self.relocs = {}
        for sec in self.elf.iter_sections():
            if sec.name.startswith('.rel.'):
                for r in sec.iter_relocations():
                    self.relocs[(sec.name[5:], r['r_offset'])] = r
        self.secs = {s.name: s for s in self.elf.iter_sections()}

    def func(self, name):
        return self.syms.get(name)

    def _sec_for(self, addr):
        for s in self.secs.values():
            if s['sh_addr'] <= addr < s['sh_addr'] + s['sh_size'] and s['sh_size']:
                return s
        return None

    def insns_arm(self, name):
        a, sz = self.syms[name]
        if not sz:
            sz = 0x40
        sec = self._sec_for(a)
        off = a - sec['sh_addr']
        data = sec.data()[off:off + sz]
        md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        return a, list(md.disasm(data, a))

    def reloc_name(self, sec_suffix, off):
        r = self.relocs.get((sec_suffix, off))
        if not r:
            return None
        sym = self.symtab.get_symbol(r['r_info_sym'])
        return sym.name, r['r_info_type']

    def call_targets(self, name):
        out = []
        a, insns = self.insns_arm(name)
        for ins in insns:
            r = self.reloc_name('text', ins.address)
            if r and r[1] in (28, 29):
                out.append(r[0])
        return a, out

    def literals_used(self, name):
        a, insns = self.insns_arm(name)
        writes = []
        for ins in insns:
            if ins.mnemonic in ('strb', 'strh', 'str') and '[' in ins.op_str and ',' in ins.op_str:
                writes.append((ins.mnemonic, ins.op_str))
        return writes

    def data_table_refs(self, func_name):
        a, _ = self.syms[func_name]
        hits = []
        for (sec_suffix, off), r in self.relocs.items():
            if sec_suffix != 'data':
                continue
            sym = self.symtab.get_symbol(r['r_info_sym'])
            if sym['st_value'] == a:
                hits.append(off)
        return sorted(hits)


def parse_pd4(path):
    d = open(path, 'rb').read()
    rows = {}
    for i in range(400):
        k, pad, w = struct.unpack_from('<HHI', d, TABLE_A - CA_BIAS + i * 8)
        if k == 0xFFFF:
            break
        rows[k] = w
    return d, rows


def worker_thumb_insns(d, ca, n=40):
    ca = ca & ~1
    off = ca - CA_BIAS
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    return list(md.disasm(d[off:off + n * 2], ca))


def main():
    if len(sys.argv) < 3:
        print("usage: sta_ops_re.py <hi5622v100_wifi.ko> <FIRMWARE.PD4.bin>")
        return 2
    ko = VendorKo(sys.argv[1])
    print("== vendor host module: %s (%d B, sha256 %s)" % (
        sys.argv[1], len(ko.raw), hashlib.sha256(ko.raw).hexdigest()[:16]))

    print("")
    print("[1] cfg80211 station ops")
    for name, expect in (('wal_cfg80211_add_station', 8),
                         ('wal_cfg80211_change_station', 8),
                         ('wal_cfg80211_del_station', 0x29c)):
        e = ko.func(name)
        check(e is not None, name + " present")
        if e:
            a, sz = e
            check(sz == expect, "%s .text 0x%x size 0x%x" % (name, a, sz))
    for name in ('wal_cfg80211_get_station', 'wal_cfg80211_dump_station'):
        check(ko.func(name) is not None, name + " present")
    offs = [ko.data_table_refs(n) for n in ('wal_cfg80211_add_station',
            'wal_cfg80211_del_station', 'wal_cfg80211_change_station',
            'wal_cfg80211_get_station', 'wal_cfg80211_dump_station')]
    check(all(len(o) == 1 for o in offs), "cfg80211_ops .data slots resolved once each")
    check(all(offs[i][0] == offs[0][0] + 4 * i for i in range(5)),
          "station op slots are consecutive .data entries")

    print("")
    print("[2] AP client station context path (vendor hmac AP SME)")
    a, calls = ko.call_targets('hmac_ap_assoc_check_chip_exist_user_proc')
    print("  hmac_ap_assoc_check_chip_exist_user_proc @ 0x%x calls: %s" % (
        a, ", ".join(calls)))
    for want in ('hmac_ap_rx_auth_chip_find_user_proc', 'hmac_user_add',
                 'mac_user_set_asoc_state', 'hmac_config_user_asoc_state_syn'):
        check(want in calls, "assoc path calls " + want)

    print("")
    print("[3] g0 id7 user_add body (hmac_user_add)")
    writes = ko.literals_used('hmac_user_add')
    for off in ('#0xc', '#0xe', '#0x9a', '#0xb0'):
        check(any(off in s for _, s in writes),
              "hmac_user_add writes data%s (uid/mac/type/rssi)" % off)
    a2, insns = ko.insns_arm('hmac_user_add')
    check(any(i.mnemonic == 'mov' and i.op_str == 'r1, #7' for i in insns),
          "hmac_user_add uses HCC id 7 (mov r1,#7)")
    check(any(i.mnemonic == 'mov' and i.op_str == 'r0, #0xa8' for i in insns),
          "hmac_user_add allocates the 168-byte body (0xa8)")
    _, calls = ko.call_targets('hmac_user_add')
    check('hcc_msg_alloc' in calls and 'hcc_msg_tx' in calls,
          "hmac_user_add posts through hcc_msg_alloc/hcc_msg_tx")

    print("")
    print("[4] g0 id1 cmd 0x07d2 association-state sync")
    a3, calls3 = ko.call_targets('hmac_config_user_asoc_state_syn')
    check('hmac_config_send_event' in calls3, "asoc sync calls hmac_config_send_event")
    a4, insns4 = ko.insns_arm('hmac_config_user_asoc_state_syn')
    check(any(i.mnemonic == 'movw' and '0x7d2' in i.op_str for i in insns4),
          "asoc sync command id 0x07d2")
    check(any(i.mnemonic == 'mov' and i.op_str == 'r2, #4' for i in insns4),
          "asoc sync body length 4")
    check(any('#0x18' in i.op_str for i in insns4),
          "asoc sync reads the AID field (user+0xa6c+0x18 = user+0xa84)")
    check(any('#0x3d' in i.op_str for i in insns4),
          "asoc sync reads the state byte (user+0xa6c+0x3d = user+0xaa9)")
    a5, insns5 = ko.insns_arm('hmac_del_user_notify_dmac')
    check(any(i.mnemonic == 'mov' and i.op_str == 'r0, #0xc' for i in insns5),
          "user_del allocates the 12-byte body")
    check(any(i.mnemonic == 'mov' and i.op_str == 'r2, #9' for i in insns5),
          "user_del uses HCC id 9")

    print("")
    print("[5] vendor .del_station -> WAL kick body")
    a6, calls6 = ko.call_targets('wal_cfg80211_del_station')
    check('wal_cfg80211_start_disconnect' in calls6,
          "vendor .del_station posts the WAL cfg-event kick builder")
    a7, insns7 = ko.insns_arm('wal_cfg80211_del_station')
    check(any(i.mnemonic in ('mov', 'moveq', 'movne') and '#0xc0' in i.op_str for i in insns7),
          "del_station maps subtype 12 to the 0xc0 deauth flag")

    print("")
    print("[6] EAPOL / data RX helpers")
    a8, calls8 = ko.call_targets('hmac_rx_ring_entry_proc_frame')
    check('hmac_rx_frame_80211_to_eth' in calls8,
          "RX frame proc calls hmac_rx_frame_80211_to_eth")
    a9, insns9 = ko.insns_arm('hmac_rx_get_mac_header_len')
    vals = [i.op_str for i in insns9 if i.mnemonic in ('mov', 'movne', 'moveq')]
    check(any('#0x18' in v for v in vals) and any('#0x1e' in v for v in vals),
          "802.11 header length: 24 B base / 30 B 4-address")
    a10, insns10 = ko.insns_arm('hmac_is_eapol_key_ptk_4_4')
    check(any(i.mnemonic == 'movw' and '0x8e88' in i.op_str for i in insns10),
          "EAPOL ethertype 0x888e (byte-swapped constant 0x8e88)")
    check(any(i.mnemonic == 'cmp' and i.op_str == 'r3, #0x5e' for i in insns10),
          "EAPOL-Key minimum length 0x5e")
    check(any(i.mnemonic == 'tst' and i.op_str == 'r3, #8' for i in insns10),
          "EAPOL-Key pairwise bit 3")
    a11, calls11 = ko.call_targets('hmac_tx_msdu_dscr_free')
    check(calls11.count('hmac_is_eapol_key_ptk_4_4') >= 2,
          "TX completion detects the 4/4-like EAPOL-Key frame (authorization hook)")

    print("")
    print("[7] PD4 firmware dispatch (table A CA 0x%x)" % TABLE_A)
    d, rows = parse_pd4(sys.argv[2])
    print("  firmware image %s (%d B, sha256 %s)" % (
        sys.argv[2], len(d), hashlib.sha256(d).hexdigest()[:16]))
    check(CFG_CMD_ASOC_SYN in rows,
          "table A has row key 0x%04x -> worker CA 0x%x" % (
              CFG_CMD_ASOC_SYN, rows.get(CFG_CMD_ASOC_SYN, 0)))
    for k, role in ((0x0078, 'add_key'), (0x0079, 'set_default_key'),
                    (0x007A, 'del_key')):
        check(k in rows, "table A row 0x%04x (%s) -> worker CA 0x%x" % (
            k, role, rows.get(k, 0)))
    check(0x01F2 not in rows,
          "table A has NO 0x01F2 row (WAL mgmt-tx is another family)")
    if CFG_CMD_ASOC_SYN in rows:
        w_ins = worker_thumb_insns(d, rows[CFG_CMD_ASOC_SYN], 40)
        check(any(i.mnemonic.startswith('ldrb') and i.op_str == 'r3, [r2, #2]'
                  for i in w_ins), "worker reads body[2] (asoc state)")
        check(any(i.mnemonic.startswith('ldrh') and i.op_str == 'r2, [r2]'
                  for i in w_ins), "worker reads body[0..1] (AID / user id)")
        check(any(i.mnemonic.startswith('strb') and i.op_str == 'r3, [r0, #0x3d]'
                  for i in w_ins),
              "worker writes the firmware user state byte at +0x3d")
        check(any(i.mnemonic == 'movs' and i.op_str == 'r0, #0x64' for i in w_ins),
              "worker returns 0x64 (user not found) on failure")

    print("")
    if failures:
        print("sta_ops_re: %d FAILURE(S)" % len(failures))
        return 1
    print("sta_ops_re: all derived facts match the lane header/tables")
    return 0


if __name__ == '__main__':
    sys.exit(main())
