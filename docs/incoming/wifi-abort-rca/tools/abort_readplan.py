#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# abort_readplan.py -- host-only (READ-ONLY) BAR0 read plan that discriminates
# the candidate objects of the firmware DATA ABORT report (wifi-abort-rca lane).
#
# It prints addresses; it never contacts the bench router.  Address rules:
#   RAM CPU address A  -> BAR0 = A + 0x6B8000   (region-5 window)
#   chip IO CA         -> BAR0 = 0x3B8000 + (CA & 0xFFFFF)  (region-3 window)
#   device CA of a RAM object = CPU - 0x40000 + 0x1240000 = CPU + 0x1200000
#
# Usage: abort_readplan.py [--block 0x...]   # block = *(ctx+0x190) once known
import argparse
BAR0_RAM = 0x6B8000
BAR0_IO  = 0x3B8000

def ram(a):  return BAR0_RAM + a
def io(ca):  return BAR0_IO + (ca & 0xFFFFF)

def show(tag, cpu, note, width=4):
    print(f'  {tag:<34} CPU {cpu:08x}  BAR0 {ram(cpu):08x}  {note}')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--block', type=lambda s:int(s,0), default=None,
                    help='value of *(ctx+0x190), the per-core record block')
    a = ap.parse_args()
    print('== Read-only confirmation plan (no writes, no device contact here) ==')
    print('-- 0. exception record itself --')
    print(f'  staged frame word15 (lr_exc-4)     CPU 00040048  BAR0 {ram(0x40048):08x}  [known 0x88bb9]')
    print(f'  staged frame word16 SPSR           CPU 0004004c  BAR0 {ram(0x4004C):08x}  [NOT YET READ: settles vector/mode]')
    print('-- 1. g object = d2h context (CA 0x10c0f4; pointer slot CA 0x172130) --')
    for off, note in [(0x9c,'out[1] pointer (live 0x40039014)'),(0xb4,'armed'),(0xb8,'pending'),
                      (0xc0,'lock'),(0x190,'per-core record-block pointer'),(0x2b8,'first record area / list heads'),
                      (0x2cc,'descriptor-block pointer used by 0x87d64'),(0x2d8,'register-block pointer used by 0x88024'),
                      (0x3cc,'second descriptor array'),(0x4b0,'per-link sub-context pointer')]:
        show(f'g+{off:#05x} {note}', 0x10C0F4+off, 'candidate obj field')
    print('-- 2. boot task object CA 0x117c00 (already partially read) --')
    for off, note in [(0x190,'per-core record-block pointer'),(0x2b8,'list heads'),
                      (0x2cc,'=0x00117ec8 (self+0x2c8)'),(0x3cc,'misc'),(0x4b0,'per-link sub-context pointer')]:
        show(f'boot+{off:#05x} {note}', 0x117C00+off, 'candidate obj field')
    print('-- 3. registry core-0 record targets (CA 0x829fb4 record: 0x103fb8,0x171b70,0x171e1c,0x171f54,0x171fa8) --')
    for base, tag in [(0x171B70,'registry target 0'),(0x171E1C,'registry target 1'),
                      (0x171F54,'registry target 2'),(0x171FA8,'registry target 3')]:
        show(f'{tag}+0x2cc', base+0x2CC, 'candidate pointer')
        show(f'{tag}+0x190', base+0x190, 'candidate per-core block pointer')
    print('-- 4. per-core descriptor block: pass --block <value of *(ctx+0x190)> --')
    if a.block is None:
        print('  (not expanded; run with --block 0x... after reading one of the +0x190 words above)')
    else:
        b = a.block
        print(f'  block = {b:#x}  (device CA {b+0x1200000:#x})')
        for i in range(4):
            rec = b + 0x2BC + 0x1C*i
            print(f'  record[{i}] start CPU {rec:08x} BAR0 {ram(rec):08x}')
            for f in range(0, 0x1C, 4):
                print(f'    +{f:#04x} CPU {rec+f:08x} BAR0 {ram(rec+f):08x}')
        print('  -> the per-core field written by 0x84664 is record[i]+0x10:')
        for i in range(4):
            rec = b + 0x2BC + 0x1C*i
            print(f'    record[{i}]+0x10 CPU {rec+0x10:08x} BAR0 {ram(rec+0x10):08x}  (0 => per-core stage not run)')
        print('  -> the descriptor-state pointer of this block used by the chain is block+0x2cc:')
        for off in (0x2B8,0x2CC,0x3CC):
            print(f'    block+{off:#05x} CPU {b+off:08x} BAR0 {ram(b+off):08x}')
    print('-- 5. IO CAs of interest (region-3 rule) --')
    for ca, note in [(0x40000108,'CPU release word'),(0x4000010c,'D2H gate (expect 0 when parked)'),
                     (0x40039014,'out[1] D2H bitmap'),(0x40101434,'D2H doorbell'),
                     (0x40114568,'PHY table register from staged r4-r11 pairs'),
                     (0x40114564,'PHY table register'),(0x40114588,'PHY table register')]:
        print(f'  {note:<52} CA {ca:08x} BAR0 {io(ca):08x}')

if __name__ == '__main__':
    main()
