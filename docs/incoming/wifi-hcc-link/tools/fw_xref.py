#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# fw_xref.py -- host-only call-graph / reachability helper for the Hi5622V100
# firmware image (wifi-hcc-link lane).
#
# It reads the LOCAL vendor firmware blob at runtime and prints derived
# annotations only; it never copies vendor bytes into the repository and never
# touches the router.
#
# Address conventions (same as the sibling lanes):
#   CPU address = file offset + 0x40000  (image loaded at CPU 0x40000)
#   default --base 0x40000; --file prints file offsets instead.
#
# Usage:
#   fw_xref.py --fw FIRMWARE.bin funcs                 # list function entries
#   fw_xref.py ... dis  <cpu> [--len 0x200]           # smart linear disassembly
#   fw_xref.py ... calls <cpu>                        # direct calls of one function
#   fw_xref.py ... callers <cpu> [cpu ...]            # direct callers (scan)
#   fw_xref.py ... graph                               # write call graph stats
#   fw_xref.py ... reach --roots 0x..,0x.. --targets 0x..,0x.. [--depth N]
#   fw_xref.py ... enc <cpu>                          # find enclosing function
#
# The call graph is built by recursive-descent from every BL/BLX target plus the
# mailbox/HCC handler roots; only direct (imm) branches are resolved.
import argparse
import os
import struct
import sys

from capstone import (Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_OP_IMM,
                      CS_OP_REG, CS_OP_MEM, CS_GRP_CALL, CS_GRP_JUMP)

DEFAULT_FW = os.environ.get('HI5622_FW',
    '/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin')
REG = ['r0','r1','r2','r3','r4','r5','r6','r7','r8','r9','r10','r11','r12','sp','lr','pc']


def load(path):
    with open(path, 'rb') as f:
        return f.read()


class InsnMap:
    """single-instruction decode map over the whole image (Thumb)."""

    def __init__(self, data, base):
        self.data = data
        self.base = base
        self.map = {}
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        md.detail = True
        self.md = md
        # decode one instruction at every even file offset
        for off in range(0, len(data) - 2, 2):
            hw = struct.unpack_from('<H', data, off)[0]
            if hw >= 0xE800:
                if off + 4 > len(data):
                    continue
                raw = data[off:off+4]
            else:
                raw = data[off:off+2]
            ins = next(iter(md.disasm(raw, base + off)), None)
            if ins is not None and ins.size == len(raw):
                self.map[base + off] = ins
            # keep a 2-byte fallback for 32-bit misdecode as 16-bit
            ins2 = next(iter(md.disasm(data[off:off+2], base + off)), None)
            if ins2 is not None and ins2.size == 2:
                self.map.setdefault(base + off, ins2)

    def at(self, addr):
        return self.map.get(addr)

    def decode_stream(self, addr, maxn=2000):
        """yield instructions linearly, following fallthrough only."""
        n = 0
        while n < maxn:
            ins = self.at(addr)
            if ins is None:
                return
            yield ins
            addr += ins.size
            n += 1


NON_BRANCH_B = {'bic', 'bfi', 'bfc', 'bkpt', 'bxj', 'bxns'}


def _base_mnemonic(ins):
    """mnemonic without the .n/.w width suffix (capstone keeps it)."""
    return ins.mnemonic.split('.')[0]


def branch_target(ins):
    """direct branch/call target if this insn is one, else None.

    Classified through capstone's group flags (not by mnemonic spelling):
    Thumb-2 conditional branches come back as 'bhi.w', 'beq.n', ... and a
    mnemonic-string test silently dropped every one of them in the first
    revision of this tool.
    """
    if not ins.operands or ins.operands[0].type != CS_OP_IMM:
        return None
    if CS_GRP_CALL in ins.groups or CS_GRP_JUMP in ins.groups:
        return ins.operands[0].imm
    return None


def is_call(ins):
    return CS_GRP_CALL in ins.groups and ins.operands and ins.operands[0].type == CS_OP_IMM


def is_uncond_branch(ins):
    return _base_mnemonic(ins) == 'b' and CS_GRP_JUMP in ins.groups


def is_cond_branch(ins):
    b = _base_mnemonic(ins)
    if b in ('b', 'bl', 'blx', 'bx') or b in NON_BRANCH_B:
        return False
    return b.startswith('b') and CS_GRP_JUMP in ins.groups


def is_terminator(ins):
    """True when control does not fall through (return / computed jump).

    Only a PC DESTINATION terminates: a literal-pool load `ldr rX,[pc,#n]` is
    an ordinary instruction and must NOT stop the traversal (a bug in the
    first revision of this tool did exactly that and silently emptied most
    function bodies).
    """
    m = ins.mnemonic
    if m in ('bx', 'bxns'):
        return True
    if m in ('udf', 'svc', 'bkpt'):
        return True
    if m.startswith('pop') or m.startswith('ldm'):
        # pop {r4, pc} / ldm sp!, {.., pc}: pc need not be operand[0].
        # Compare through capstone's register NAMES: operand.reg is a
        # capstone register id, not an architectural register number.
        return any(o.type == CS_OP_REG and ins.reg_name(o.reg) == 'pc'
                   for o in ins.operands)
    if ins.operands and ins.operands[0].type == CS_OP_REG and \
       ins.reg_name(ins.operands[0].reg) == 'pc':
        return True          # ldr pc / mov pc / add pc
    return False


class CallGraph:
    def __init__(self, im, base):
        self.im = im
        self.base = base
        # first pass: function entries = all direct BL/BLX targets
        entries = set()
        for addr, ins in im.map.items():
            if ins.mnemonic in ('bl', 'blx') and ins.operands and ins.operands[0].type == CS_OP_IMM:
                t = ins.operands[0].imm
                if base <= t < base + len(im.data):
                    entries.add(t)
        self.entries = entries
        self.calls = {}       # func -> set(callee)
        self.callers = {}     # func -> set(caller)
        for f in entries:
            self._descend(f)

    def _descend(self, f):
        """Recursive-descent one function body.

        The visited-instruction set is PER FUNCTION: kernel firmware functions
        are reached by indirect dispatch (data tables) and tail branches can
        bleed from one body into the next, so a global visited set would
        leave functions that were touched by an earlier descent with an EMPTY
        callee set (a silent false negative).  Attribution of a tail target's
        callees to the branch source only over-approximates reachability.
        """
        if f in self.calls:
            return
        self.calls[f] = set()
        work = [f]
        seen = set()
        while work:
            a = work.pop()
            if a in seen or a not in self.im.map:
                continue
            seen.add(a)
            ins = self.im.map[a]
            tgt = branch_target(ins)
            nxt = a + ins.size
            if is_call(ins):
                if tgt is not None:
                    self.calls[f].add(tgt)
                    self.callers.setdefault(tgt, set()).add(f)
                if nxt in self.im.map:
                    work.append(nxt)
            elif is_uncond_branch(ins):
                if tgt is not None:
                    self.calls[f].add(tgt)
                    self.callers.setdefault(tgt, set()).add(f)
                    # a known function entry is a TAIL CALL: record the edge
                    # but do not adopt the callee body (that would attribute
                    # the callee's callees to us and over-approximate reach).
                    if tgt not in self.entries and tgt in self.im.map:
                        work.append(tgt)
                # tail call / no fallthrough
            elif is_cond_branch(ins) and tgt is not None:
                self.calls[f].add(tgt)
                self.callers.setdefault(tgt, set()).add(f)
                work.append(tgt)
                if nxt in self.im.map:
                    work.append(nxt)
            elif ins.mnemonic in ('cbz', 'cbnz'):
                if ins.operands[1].type == CS_OP_IMM:
                    work.append(ins.operands[1].imm)
                if nxt in self.im.map:
                    work.append(nxt)
            elif is_terminator(ins):
                # return / computed jump: no fallthrough
                pass
            else:
                if nxt in self.im.map:
                    work.append(nxt)

    def enclosing(self, addr):
        best = None
        for f in self.entries:
            if f <= addr and (best is None or f > best):
                best = f
        return best

    def reach(self, roots, targets, maxdepth=99):
        """BFS over call edges; returns path root->target or None."""
        tset = set(targets)
        for r in roots:
            if r in tset:
                return [r]
            # roots and targets may be data-table handlers (never a BL target);
            # descend them lazily so their bodies are in the graph.
            if r not in self.calls:
                self._descend(r)
            for t in tset:
                if t not in self.calls:
                    self._descend(t)
            prev = {r: None}
            q = [(r, 0)]
            depth = {r: 0}
            while q:
                cur, d = q.pop(0)
                if d >= maxdepth:
                    continue
                if cur not in self.calls:
                    self._descend(cur)
                for c in sorted(self.calls.get(cur, ())):
                    if c in prev:
                        continue
                    prev[c] = cur
                    depth[c] = d + 1
                    if c in tset:
                        path = [c]
                        while prev[path[-1]] is not None:
                            path.append(prev[path[-1]])
                        return list(reversed(path))
                    q.append((c, d + 1))
        return None


def fmt(a, base, file_off):
    return f'{a - base:#08x}' if file_off else f'{a:#08x}'


def main():
    p = argparse.ArgumentParser(description='Hi5622V100 firmware call-graph helper (host-only)')
    p.add_argument('--fw', default=DEFAULT_FW)
    p.add_argument('--base', type=lambda s: int(s, 0), default=0x40000)
    p.add_argument('--file', action='store_true', help='print file offsets')
    sub = p.add_subparsers(dest='cmd', required=True)

    q = sub.add_parser('dis'); q.add_argument('cpu', type=lambda s: int(s, 0)); q.add_argument('--len', type=lambda s: int(s, 0), default=0x200)
    q = sub.add_parser('calls'); q.add_argument('cpu', type=lambda s: int(s, 0)); q.add_argument('--all', action='store_true')
    q = sub.add_parser('callers'); q.add_argument('cpus', nargs='+', type=lambda s: int(s, 0))
    q = sub.add_parser('funcs')
    q = sub.add_parser('enc'); q.add_argument('cpu', type=lambda s: int(s, 0))
    q = sub.add_parser('reach')
    q.add_argument('--roots', required=True); q.add_argument('--targets', required=True)
    q.add_argument('--depth', type=int, default=99)
    q = sub.add_parser('targets')  # list call edges of the image for a target set
    q.add_argument('cpus', nargs='+', type=lambda s: int(s, 0))

    a = p.parse_args()
    d = load(a.fw)
    if a.cmd in ('dis',):
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        md.detail = True
        off = a.cpu - a.base
        for ins in md.disasm(d[off:off + a.len], a.cpu):
            print(f'{fmt(ins.address, a.base, a.file)}: {ins.bytes.hex():<16} {ins.mnemonic:<9} {ins.op_str}')
        return 0
    im = InsnMap(d, a.base)
    cg = CallGraph(im, a.base)
    if a.cmd == 'funcs':
        for f in sorted(cg.entries):
            print(fmt(f, a.base, a.file))
        return 0
    if a.cmd == 'enc':
        f = cg.enclosing(a.cpu)
        print(f'enclosing entry: {fmt(f, a.base, a.file) if f is not None else None}')
        return 0
    if a.cmd == 'calls':
        s = cg.calls.get(a.cpu)
        if s is None:
            f = cg.enclosing(a.cpu)
            s = cg.calls.get(f, set())
            print(f'(not an entry; using enclosing {fmt(f, a.base, a.file)})')
        for c in sorted(s):
            print(f'{fmt(c, a.base, a.file)}')
        return 0
    if a.cmd == 'callers':
        want = set(a.cpus)
        for f, callees in sorted(cg.calls.items()):
            hit = callees & want
            if hit:
                print(f'{fmt(f, a.base, a.file)} -> ' + ', '.join(fmt(x, a.base, a.file) for x in sorted(hit)))
        return 0
    if a.cmd == 'targets':
        want = set(a.cpus)
        for r in want:
            print(f'target {fmt(r, a.base, a.file)}:')
            for c in sorted(cg.callers.get(r, ())):
                print(f'  caller {fmt(c, a.base, a.file)}')
        return 0
    if a.cmd == 'reach':
        roots = [int(x, 0) for x in a.roots.split(',')]
        targets = [int(x, 0) for x in a.targets.split(',')]
        for r in roots:
            path = cg.reach([r], targets, a.depth)
            if path:
                print(f'{fmt(r, a.base, a.file)}: ' + ' -> '.join(fmt(x, a.base, a.file) for x in path))
        return 0
    return 2


if __name__ == '__main__':
    raise SystemExit(main())
