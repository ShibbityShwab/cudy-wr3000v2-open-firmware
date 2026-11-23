#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# ko_xref.py -- relocation-based function call graph for the vendor ARM kernel
# modules (wifi-hcc-link lane).  Reads the LOCAL .ko files at runtime; prints
# derived annotations only.  Nothing vendor is copied into the repository.
#
# ET_REL kernel modules keep an R_ARM_CALL / R_ARM_JUMP24 relocation for every
# cross-function call (including calls to same-module globals), so the call graph
# can be built from relocations + the (unstripped) symtab, with no heuristic
# disassembly of branch targets needed.
#
# usage:
#   ko_xref.py <ko> callers  <sym> [sym ...]
#   ko_xref.py <ko> callees  <sym> [sym ...]
#   ko_xref.py <ko> reach    <root_syms_csv> <target_syms_csv> [--depth N]
#   ko_xref.py <ko> senders
#   ko_xref.py <ko> desc     <root_sym> [--depth N]
#   ko_xref.py <ko> list     <regex>
#   ko_xref.py <ko> writes   <sym>        # ABS32/MOVW+MOVT data refs of sym
import argparse
import re
import sys

from elftools.elf.elffile import ELFFile

CALL_TYPES = {'R_ARM_CALL', 'R_ARM_JUMP24', 'R_ARM_THM_CALL', 'R_ARM_THM_JUMP24'}

SEND_SYMS = {
    'hcc_msg_tx', 'hcc_msg_tx_to_core', 'hcc_msg_alloc', 'hcc_msg_alloc_no_cross_pool',
    'hcc_msg_clone', 'hcc_msg_post_msg', 'hcc_msg_post_vap_msg', 'hcc_queue_post_msg',
    'hcc_queue_msg_process', 'bal_msg_send', 'pcie_msg_send', 'pcie_msg_send_irq',
}


def reloc_type_name(e, rt):
    try:
        from elftools.elf.enums import ENUM_RELOC_TYPE_ARM
        return {v: k for k, v in ENUM_RELOC_TYPE_ARM.items()}.get(rt, str(rt))
    except Exception:
        return str(rt)


class Ko:
    def __init__(self, path):
        self.fh = open(path, 'rb')
        self.e = ELFFile(self.fh)
        self.st = self.e.get_section_by_name('.symtab')
        self.sec_index = {}
        for i, sec in enumerate(self.e.iter_sections()):
            self.sec_index[sec.name] = i
        # function table: name -> (shndx, addr, size, section_base)
        self.funcs = {}
        for s in self.st.iter_symbols():
            if s['st_info']['type'] != 'STT_FUNC' or not s.name or s['st_shndx'] == 'SHN_UNDEF':
                continue
            sec = self.e.get_section(s['st_shndx'])
            self.funcs.setdefault(s.name, (s['st_shndx'], sec['sh_addr'] + s['st_value'], s['st_size']))
        self.by_section = {}
        for name, (shndx, addr, size) in self.funcs.items():
            self.by_section.setdefault(shndx, []).append((addr, size, name))
        self.edges = {}        # caller -> set(callee)
        self.data_refs = {}    # symbol -> list((caller, kind))
        self._build()

    def enclosing(self, shndx, addr):
        best = None
        for a, size, name in self.by_section.get(shndx, ()):
            if a <= addr < a + max(size, 1):
                if best is None or a > best[0]:
                    best = (a, name)
        return best[1] if best else None

    def _build(self):
        for sec in self.e.iter_sections():
            if not sec.name.startswith('.rel'):
                continue
            tgt = self.e.get_section_by_name(sec.name[4:])
            if tgt is None:
                continue
            rs = sec
            try:
                st = self.e.get_section(rs['sh_link'])
            except Exception:
                st = self.st
            for r in rs.iter_relocations():
                tname = reloc_type_name(self.e, r['r_info_type'])
                sym = st.get_symbol(r['r_info_sym'])
                sname = sym.name
                if not sname:
                    continue
                if tname in CALL_TYPES:
                    fn = self.enclosing(self.sec_index.get(tgt.name), r['r_offset'])
                    if fn:
                        self.edges.setdefault(fn, set()).add(sname)
                else:
                    fn = self.enclosing(self.sec_index.get(tgt.name), r['r_offset'])
                    if fn and tname in ('R_ARM_ABS32', 'R_ARM_MOVW_ABS_NC', 'R_ARM_MOVT_ABS',
                                        'R_ARM_PREL31', 'R_ARM_MOVW_PREL_NC', 'R_ARM_MOVT_PREL'):
                        self.data_refs.setdefault(sname, []).append((fn, tname))

    def calls(self, fn):
        return self.edges.get(fn, set())

    def reach(self, root, targets, depth=99):
        prev = {root: None}
        q = [(root, 0)]
        while q:
            cur, d = q.pop(0)
            if d >= depth:
                continue
            for c in sorted(self.calls(cur)):
                if c in prev:
                    continue
                prev[c] = cur
                if c in targets:
                    path = [c]
                    while prev[path[-1]] is not None:
                        path.append(prev[path[-1]])
                    return list(reversed(path))
                q.append((c, d + 1))
        return None

    def ancestors(self, targets):
        rev = {}
        for fn, callees in self.edges.items():
            for c in callees:
                rev.setdefault(c, set()).add(fn)
        seen = {}
        q = [(t, 0) for t in targets]
        for t in targets:
            seen[t] = 0
        while q:
            cur, d = q.pop(0)
            for p in rev.get(cur, ()):
                if p not in seen or seen[p] > d + 1:
                    seen[p] = d + 1
                    q.append((p, d + 1))
        return seen


def main():
    p = argparse.ArgumentParser()
    p.add_argument('ko')
    sub = p.add_subparsers(dest='cmd', required=True)
    q = sub.add_parser('callers'); q.add_argument('names', nargs='+')
    q = sub.add_parser('callees'); q.add_argument('names', nargs='+')
    q = sub.add_parser('reach'); q.add_argument('roots'); q.add_argument('targets'); q.add_argument('--depth', type=int, default=99)
    q = sub.add_parser('senders')
    q = sub.add_parser('desc'); q.add_argument('root'); q.add_argument('--depth', type=int, default=99)
    q = sub.add_parser('list'); q.add_argument('pattern')
    q = sub.add_parser('writes'); q.add_argument('sym')
    q = sub.add_parser('stats')
    a = p.parse_args()
    k = Ko(a.ko)
    if a.cmd == 'callers':
        for t in a.names:
            res = sorted(fn for fn, cs in k.edges.items() if t in cs)
            print(f'{t}: ' + (', '.join(res) if res else '<none>'))
    elif a.cmd == 'callees':
        for t in a.names:
            print(f'{t}: ' + ', '.join(sorted(k.calls(t))))
    elif a.cmd == 'reach':
        roots = a.roots.split(','); targets = set(a.targets.split(','))
        for r in roots:
            path = k.reach(r, targets, a.depth)
            print(f'{r}: ' + (' -> '.join(path) if path else 'NO PATH'))
    elif a.cmd == 'senders':
        for fn in sorted(k.edges):
            hit = k.edges[fn] & SEND_SYMS
            if hit:
                print(f'{fn} -> ' + ', '.join(sorted(hit)))
    elif a.cmd == 'desc':
        prev = {a.root: None}
        q = [(a.root, 0)]
        while q:
            cur, d = q.pop(0)
            if d >= a.depth:
                continue
            for c in sorted(k.calls(cur)):
                if c not in prev:
                    prev[c] = cur
                    q.append((c, d + 1))
        for c in sorted(prev):
            if c != a.root:
                print(c)
    elif a.cmd == 'list':
        pat = re.compile(a.pattern)
        for n in sorted(k.funcs):
            if pat.search(n):
                print(n)
    elif a.cmd == 'writes':
        for fn, kind in k.data_refs.get(a.sym, ()):
            print(f'{fn}  ({kind})')
    elif a.cmd == 'stats':
        print(f'functions: {len(k.funcs)}  callers: {len(k.edges)}  edges: {sum(len(v) for v in k.edges.values())}')
        print(f'targets with no caller: {sum(1 for f in k.funcs if not any(f in v for v in k.edges.values()))}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
