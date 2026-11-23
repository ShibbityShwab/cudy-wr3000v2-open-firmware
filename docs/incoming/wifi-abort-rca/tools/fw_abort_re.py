#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# fw_abort_re.py -- host-only disassembly/annotation helper for the Hi5622V100
# firmware image (wifi-abort-rca lane).  It reads the LOCAL vendor firmware
# blob at runtime and prints derived annotations; it never copies vendor bytes
# into the repository and never touches the bench router.
#
# Address conventions used by the firmware lane (see REPORT.md section 0):
#   CPU address  = file offset + 0x40000        (image is loaded at CPU 0x40000)
#   Host BAR0    = CPU + 0x6B8000               (region-5 16 MiB window)
#   Host BAR0    = 0x3B8000 + (CA & 0xFFFFF)    (region-3 chip-IO window)
#
# Usage:
#   fw_abort_re.py dis  --cpu 0x87D64 --len 0xC0 [--mode thumb|arm]
#   fw_abort_re.py frame                       # decode the staged frame layout
#   fw_abort_re.py callers 0x88AFC 0x87D64 ... # BL/BLX caller scan
#   fw_abort_re.py fields 0x2B8 0x2CC 0x3CC    # LDR/STR imm12 field-ref scan
#   fw_abort_re.py reach 0x88AFC 0x87D64       # is target decoded from source?
#
# --fw defaults to $HI5622_FW or the known local path; --base defaults to 0x40000.
import argparse, os, struct, sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_OP_IMM, CS_OP_MEM, CS_OP_REG

DEFAULT_FW = os.environ.get('HI5622_FW',
    '/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin')
REG = ['r0','r1','r2','r3','r4','r5','r6','r7','r8','r9','r10','r11','r12','sp','lr','pc']

def load(path):
    with open(path,'rb') as f: return f.read()

def md_for(mode):
    m = Cs(CS_ARCH_ARM, CS_MODE_THUMB if mode=='thumb' else CS_MODE_ARM)
    m.detail = True
    return m

def cmd_dis(d, base, a):
    m = md_for(a.mode)
    data = d[a.cpu-base:a.cpu-base+a.len]
    for i in m.disasm(data, a.cpu):
        print(f'{i.address:08x}: {i.bytes.hex():<18} {i.mnemonic:<10} {i.op_str}')

def cmd_frame(d, base, a):
    print('Staged exception record (image layout, stubs at CPU 0x103090/0x1030BC/0x1030E8/0x103114):')
    print('  CA 0x40008 (BAR0 0x6f8008) = halfword exception-seen mask, bit = 1<<r1')
    print('  CA 0x4000c + 4*i (BAR0 0x6f800c + 4*i):')
    print('    i=0..12 : r0..r12 of the faulted context')
    print('    i=13    : SVC-banked SP (read after cps #0x13)')
    print('    i=14    : SVC-banked LR')
    print('    i=15    : LR at handler entry minus 4 (data abort/prefetch/svc paths);')
    print('              minus 8 for the undef path (thunk CPU 0xc6fc8)')
    print('    i=16    : SPSR (CPSR of the faulted context) at CA 0x4004c / BAR0 0x6f804c')
    print('  vector id -> thunk: 1=0xc6fea (data abort), 2=0xc6fe0 (prefetch), 3=0xc6fd6 (svc), 4=0xc6fc8 (undef)')

def cmd_callers(d, base, a):
    m = md_for('thumb')
    targets = set(a.targets)
    for off in range(0, len(d)-4, 2):
        addr = base + off
        for i in m.disasm(d[off:off+4], addr):
            if len(i.bytes)==4 and i.mnemonic in ('bl','blx') and i.operands and i.operands[0].type==CS_OP_IMM:
                if i.operands[0].imm in targets:
                    print(f'caller CPU {addr:08x} file {addr-base:#08x} {i.mnemonic} -> CPU {i.operands[0].imm:08x}')
            break

def cmd_fields(d, base, a):
    want = set(a.offsets)
    for off in range(0, len(d)-4, 2):
        h1 = struct.unpack_from('<H', d, off)[0]
        h2 = struct.unpack_from('<H', d, off+2)[0]
        # Thumb-2 LDR/STR (imm12) forms
        if (h1 & 0xFFF0) in (0xF8C0,0xF8D0):
            imm = h2 & 0xFFF
            if imm in want:
                kind = 'str.w' if (h1 & 0xFFF0)==0xF8C0 else 'ldr.w'
                print(f'CPU {base+off:08x} file {off:#08x} {kind} {REG[(h2>>12)&0xF]},[{REG[h1&0xF]},#{imm:#x}]')
    w = set(a.offsets_arm if hasattr(a,'offsets_arm') else [])

def cmd_reach(d, base, a):
    m = md_for('thumb')
    for i in m.disasm(d[a.src-base:a.dst-a.src+8], a.src):
        if i.address == a.dst:
            print(f'{a.dst:#x} is an instruction boundary when decoding from {a.src:#x}'); return
        if i.address > a.dst: break
    print(f'{a.dst:#x} is NOT reached by linear Thumb decode from {a.src:#x}')

def main():
    p = argparse.ArgumentParser(description='Hi5622V100 firmware image disassembly/annotation helper (host-only)')
    p.add_argument('--fw', default=DEFAULT_FW)
    p.add_argument('--base', type=lambda s:int(s,0), default=0x40000)
    sub = p.add_subparsers(dest='cmd', required=True)
    q = sub.add_parser('dis'); q.add_argument('--cpu',type=lambda s:int(s,0),required=True); q.add_argument('--len',type=lambda s:int(s,0),default=0x80); q.add_argument('--mode',choices=('thumb','arm'),default='thumb')
    q = sub.add_parser('frame')
    q = sub.add_parser('callers'); q.add_argument('targets', nargs='+', type=lambda s:int(s,0))
    q = sub.add_parser('fields'); q.add_argument('offsets', nargs='+', type=lambda s:int(s,0))
    q = sub.add_parser('reach'); q.add_argument('src',type=lambda s:int(s,0)); q.add_argument('dst',type=lambda s:int(s,0))
    a = p.parse_args()
    d = load(a.fw)
    if a.cmd=='dis': cmd_dis(d,a.base,a)
    elif a.cmd=='frame': cmd_frame(d,a.base,a)
    elif a.cmd=='callers': cmd_callers(d,a.base,a)
    elif a.cmd=='fields': cmd_fields(d,a.base,a)
    elif a.cmd=='reach': cmd_reach(d,a.base,a)

if __name__ == '__main__':
    main()
