#!/usr/bin/env python3
"""annotate.py - relocation-aware annotator for ARM (.ko) disassembly.

Host-only reversal helper for the wifi-host-replica lane.  It parses the ELF
symtab + SHT_REL sections of a vendor .ko, then walks an objdump -dr listing
and appends, to every relocation line, the resolved target:

  * R_ARM_MOVW/MOVT  -> symbol + computed section:offset, and the string when
                        the target is .rodata/.rodata.str1.4
  * R_ARM_ABS32      -> section:offset of the literal word (string or data)
  * R_ARM_CALL/JUMP24-> target function section:offset

Usage:
  objdump -dr --no-show-raw-insn mod.ko > mod.s        (host toolchain objdump)
  python3 annotate.py mod.ko mod.s mod.ann.s

No vendor text/bytes are emitted by this tool; only derived addresses/strings.
"""
import struct
import sys

SEC_FMT = '<IIIIIIIIII'
SYM_FMT = '<IIIBBH'
NEWLINE = chr(10)
TAB = chr(9)


class Elf:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        d = self.d
        if d[:4] != bytes([0x7f, 0x45, 0x4c, 0x46]):
            raise SystemExit('not an ELF file: ' + path)
        e_shoff = struct.unpack_from('<I', d, 0x20)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', d, 0x2e)
        self.secs = []
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            (nameoff, typ, flags, addr, soff, size, link, info,
             align, entsize) = struct.unpack_from(SEC_FMT, d, off)
            self.secs.append(dict(idx=i, nameoff=nameoff, type=typ, flags=flags,
                                  addr=addr, off=soff, size=size, link=link,
                                  info=info, align=align, entsize=entsize))
        shstr = self.secs[e_shstrndx]
        for s in self.secs:
            e = d.index(bytes([0]), shstr['off'] + s['nameoff'])
            s['name'] = d[shstr['off'] + s['nameoff']:e].decode('utf8', 'replace')
        self.by_name = {}
        for s in self.secs:
            self.by_name.setdefault(s['name'], s)
        self.syms = []
        st = next((s for s in self.secs if s['type'] == 2), None)
        if st is None:
            raise SystemExit('no symtab: stripped module?')
        for i in range(st['size'] // 16):
            nm, val, size, info, other, shndx = struct.unpack_from(
                SYM_FMT, d, st['off'] + i * 16)
            self.syms.append(dict(nameoff=nm, value=val, size=size, info=info,
                                  other=other, shndx=shndx))
        strtab = self.secs[st['link']]
        for s in self.syms:
            e = d.index(bytes([0]), strtab['off'] + s['nameoff'])
            s['name'] = d[strtab['off'] + s['nameoff']:e].decode('utf8', 'replace')
        self.relocs = {}
        for s in self.secs:
            if s['type'] != 9:
                continue
            m = self.relocs.setdefault(s['info'], {})
            for i in range(s['size'] // 8):
                roff, rinfo = struct.unpack_from('<II', d, s['off'] + i * 8)
                m[roff] = (rinfo >> 8, rinfo & 0xff)

    def sec_at(self, idx):
        for s in self.secs:
            if s['idx'] == idx:
                return s
        return None

    def raw(self, secname, off, n):
        s = self.by_name[secname]
        return self.d[s['off'] + off:s['off'] + off + n]

    def cstr(self, secname, off, maxlen=160):
        s = self.by_name[secname]
        if s['type'] == 8 or off >= s['size']:
            return ''
        base = s['off'] + off
        end = self.d.find(bytes([0]), base, min(base + maxlen, s['off'] + s['size']))
        if end < 0:
            end = min(base + maxlen, s['off'] + s['size'])
        return self.d[base:end].decode('utf8', 'replace')

    def word_at(self, secname, off):
        s = self.by_name[secname]
        if off + 4 > s['size']:
            return None
        return struct.unpack_from('<I', self.d, s['off'] + off)[0]

    def target_of(self, sec_idx, off):
        m = self.relocs.get(sec_idx)
        if not m or off not in m:
            return None
        symi, rtype = m[off]
        if symi == 0:
            return None
        sym = self.syms[symi]
        sec = self.sec_at(sec_idx)
        word = struct.unpack('<I', self.raw(sec['name'], off, 4))[0]
        if rtype == 2:                          # R_ARM_ABS32
            addend = word
        elif rtype in (43, 45):                 # R_ARM_MOVW_*
            addend = ((word >> 4) & 0xf000) | (word & 0xfff)
        elif rtype in (44, 46):                 # R_ARM_MOVT_*
            addend = (((word >> 16) & 0xf) << 12 | (word & 0xfff)) << 16
        else:
            addend = word
        if sym['shndx'] == 0:
            return (sym, addend, None, None)
        tsec = self.sec_at(sym['shndx'])
        return (sym, addend, tsec, sym['value'] + addend)


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: annotate.py mod.ko mod.s out.s')
    elf = Elf(sys.argv[1])
    lines = open(sys.argv[2], errors='replace').read().split(NEWLINE)
    out = open(sys.argv[3], 'w')
    cursec = None
    relpat = __import__('re').compile(
        '^[ ' + TAB + ']*([0-9a-f]+): R_ARM_([^ ' + TAB + ']+)[ ' + TAB + ']+([^ ' + TAB + ']+)')
    for line in lines:
        line = line.rstrip(NEWLINE)
        if line.startswith('Disassembly of section '):
            cursec = line[len('Disassembly of section '):-1]
            out.write(line + NEWLINE)
            continue
        m = relpat.match(line)
        if m and cursec is not None:
            addr = int(m.group(1), 16)
            sec = elf.by_name.get(cursec)
            note = ''
            if sec is not None:
                t = elf.target_of(sec['idx'], addr)
                if t:
                    sym, addend, tsec, toff = t
                    if tsec is not None:
                        if 'rodata' in tsec['name'] or 'str' in tsec['name']:
                            note = ' ; -> %s+0x%x "%s"' % (
                                tsec['name'], toff, elf.cstr(tsec['name'], toff))
                        elif tsec['name'].startswith('.text'):
                            note = ' ; -> %s+0x%x' % (tsec['name'], toff)
                        else:
                            note = ' ; -> %s+0x%x' % (tsec['name'], toff)
                            if tsec['type'] == 1:
                                t2 = elf.target_of(tsec['idx'], toff)
                                if t2 and t2[2] is not None:
                                    note += ' [%s]' % t2[0]['name']
            out.write(line + note + NEWLINE)
            continue
        out.write(line + NEWLINE)
    out.close()


if __name__ == '__main__':
    main()
