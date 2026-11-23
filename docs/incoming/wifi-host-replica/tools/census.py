#!/usr/bin/env python3
"""census.py - report every call site of a set of functions in an annotated
disassembly produced by annotate.py (function + 10 preceding lines).

Usage:
  python3 census.py mod.ann.s pcie_msg_send,bal_msg_send,pcie_msg_register
"""
import re
import sys

NEWLINE = chr(10)


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: census.py mod.ann.s func1,func2,...')
    path = sys.argv[1]
    names = set(sys.argv[2].split(','))
    lines = open(path, errors='replace').read().split(NEWLINE)
    hdr = re.compile('^([0-9a-f]+) <([^>]+)>:$')
    rel = re.compile('^[ ' + chr(9) + ']*([0-9a-f]+): R_ARM_(CALL|JUMP24)[ '
                     + chr(9) + ']+([^ ' + chr(9) + ']+)')
    cur = None
    for i, line in enumerate(lines):
        m = hdr.match(line)
        if m:
            cur = m.group(2)
            continue
        m = rel.match(line)
        if m and cur and m.group(3) in names:
            print('--- %s @%s calls %s' % (cur, m.group(1), m.group(3)))
            for prev in lines[max(0, i - 10):i + 1]:
                print('   ', prev.rstrip())


if __name__ == '__main__':
    main()
