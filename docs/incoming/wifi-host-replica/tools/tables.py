#!/usr/bin/env python3
"""tables.py - dump a .data/.rodata table of a vendor .ko with relocations
resolved (function pointers, string pointers, raw words).

Usage:
  python3 tables.py mod.ko .data 0x2660 5 12      # 5 entries, 12-byte stride
  python3 tables.py mod.ko .rodata 0x101c 7 12
  python3 tables.py mod.ko .rodata 0xcac 11 8

Reuses the ELF reader from annotate.py (same directory).
"""
import sys

from annotate import Elf

NEWLINE = chr(10)


def describe(elf, sec, off):
    t = elf.target_of(sec['idx'], off)
    word = elf.word_at(sec['name'], off)
    if t and t[2] is not None:
        tsec, toff, sym = t[2], t[3], t[0]
        if 'rodata' in tsec['name'] or 'str' in tsec['name']:
            return '%s+0x%x "%s"' % (tsec['name'], toff, elf.cstr(tsec['name'], toff))
        return '%s+0x%x %s' % (tsec['name'], toff, sym['name'])
    if word is None:
        return 'oob'
    return '0x%08x' % word


def main():
    if len(sys.argv) < 5:
        raise SystemExit('usage: tables.py mod.ko section offset count [stride]')
    elf = Elf(sys.argv[1])
    name = sys.argv[2]
    off = int(sys.argv[3], 0)
    cnt = int(sys.argv[4], 0)
    stride = int(sys.argv[5]) if len(sys.argv) > 5 else 4
    sec = elf.by_name[name]
    print('== %s+0x%x: %d entries, stride %d' % (name, off, cnt, stride))
    for i in range(cnt):
        row = []
        for j in range(0, stride, 4):
            row.append(describe(elf, sec, off + i * stride + j))
        print('  +0x%03x: %s' % (off + i * stride, ' | '.join(row)))


if __name__ == '__main__':
    main()
