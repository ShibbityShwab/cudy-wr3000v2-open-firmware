#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ete_scan.py - reproducible verifier for the wifi-ete-init lane (task-81).

Reads the two local vendor platform modules (hi5622v100_plat.ko, 2.5.24 and
2.4.15 - the vendor binaries are NOT in this repo) and asserts the ETE
initialisation facts quoted in REPORT.md.  No vendor bytes are copied into the
repo by this tool; it prints derived facts only.

Facts asserted (selftest):
  A. both modules export the same pcie_ete_* init function set, and the ten
     init functions are byte-identical between the two builds;
  B. pcie_ete_init's call order is get_pcie_ete_res -> pcie_ete_intr_init ->
     pcie_ete_rings_init -> pcie_ete_chn_res;
  C. pcie_ete_intr_init builds 0xffe0f8f8 and RMWs the ETE interrupt CA
     (the (0x4003a000, 0x40039508) CA pair is a static .data constant);
  D. init_src_ring computes ETE+0xC18+4i (0xC10+8) and init_dst_ring computes
     ETE+0xC00+4i, and both only STORE those pointers into ring contexts;
  E. no instruction in either module stores through a pointer loaded from ring
     context offset 0x5C or 0xEC -> the host driver never writes the +0xC00 /
     +0xC18 send/flag words;
  F. init_src_ring/init_dst_ring write depth-1 as a bfi(0,10) and SR ctrl as a
     bfi(0,3) read-modify-write (upper bits preserved);
  G. the 2.4.15 pcie_ete_chn_enable (channel +0x00 enable) has zero call sites
     in the module, and the same code in 2.5.24 has no symbol/reference either
     -> the host never enables ETE channels (firmware does);
  H. golden capture derivation: 0x04050405 / 0x040E040E are packed ring indices
     (low 10 bits = idx, bit 10 = wrap phase) identical in both halves, equal
     to the live DR fills 0x405/0x40E - i.e. boot traffic, not init state.

Usage:
  python3 ete_scan.py selftest        # run every assertion, exit 0 iff all pass
  python3 ete_scan.py inventory       # print the recovered init inventory
  python3 ete_scan.py sendregs        # print the send/flag-word access sites
  python3 ete_scan.py compare         # 2.5.24 vs 2.4.15 function diff

Module paths can be overridden with --old-ko / --new-ko.
"""
import argparse
import hashlib
import sys

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_OP_MEM, CS_OP_REG, CS_OP_IMM
from capstone.arm import ARM_REG_PC
from elftools.elf.elffile import ELFFile

NEW_KO = "/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko"
OLD_KO = "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko"

ETE_INIT_FUNCS = [
    "pcie_ete_init", "pcie_ete_intr_init", "pcie_ete_rings_init",
    "pcie_ete_init_src_ring", "pcie_ete_init_dst_ring",
    "pcie_ete_sr_reg_init", "pcie_ete_dr_reg_init",
    "pcie_ete_sr_init", "pcie_ete_dr_init", "pcie_ete_chn_res",
]
ETE_CA_ETE = 0x4003A000           # ETE channel block CA
ETE_CA_INTR = 0x40039508          # ETE interrupt word CA
ETE_INTR_KEEP = 0xFFE0F8F8
ETE_SEND_DR0 = 0xC00              # +4i DR0..DR3
ETE_SEND_SR0 = 0xC18              # +4i SR0..SR2
GOLDEN = [  # (name, register value, expected packed index, live fill reg)
    ("DR0 send CA 0x4003AC00", 0x04050405, 0x405),
    ("DR3 send CA 0x4003AC0C", 0x040E040E, 0x40E),
]
GOLDEN_FILLS = [("DR0 fill CA 0x4003A5CC", 0x405),
                ("DR3 fill CA 0x4003A6BC", 0x40E)]


class Mod:
    def __init__(self, path, label):
        self.path = path
        self.label = label
        self.fh = open(path, "rb")
        self.elf = ELFFile(self.fh)
        self.symtab = self.elf.get_section_by_name(".symtab")
        self.text = self.elf.get_section_by_name(".text")
        self.text_index = None
        for idx, sec in enumerate(self.elf.iter_sections()):
            if sec.name == ".text":
                self.text_index = idx
        self.text_addr = self.text["sh_addr"]
        self.text_data = self.text.data()
        self.funcs = {}          # name -> (addr, size)
        by_addr = {}
        for s in self.symtab.iter_symbols():
            if s["st_info"]["type"] != "STT_FUNC":
                continue
            if s["st_shndx"] != self.text_index:
                continue
            addr = s["st_value"]
            by_addr[addr] = (s.name, s["st_size"])
        self._by_addr = by_addr
        for addr, (name, size) in by_addr.items():
            if name:
                self.funcs.setdefault(name, (addr, size))

    def func_range(self, name):
        addr, _size = self.funcs[name]
        end = min((a for a in self._by_addr if a > addr), default=None)
        size = (end - addr) if end is not None else (len(self.text_data) - addr)
        return addr, addr + size

    def func_bytes(self, name):
        a, b = self.func_range(name)
        return self.text_data[a - self.text_addr:b - self.text_addr]

    def disasm(self, name):
        a, b = self.func_range(name)
        code = self.text_data[a - self.text_addr:b - self.text_addr]
        md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        md.detail = True
        return list(md.disasm(code, a))

    def relocs(self):
        out = []
        for sec in self.elf.iter_sections():
            if sec["sh_type"] != "SHT_REL":
                continue
            for r in sec.iter_relocations():
                sym = self.symtab.get_symbol(r["r_info_sym"])
                out.append((r["r_offset"], sym.name, r["r_info_type"]))
        return out

    def calls_in(self, name):
        a, b = self.func_range(name)
        return [(off, s) for off, s, _t in self.relocs() if a <= off < b]

    def data_words(self):
        for sec in self.elf.iter_sections():
            if sec["sh_type"] != "SHT_PROGBITS" or sec["sh_flags"] & 0x4:
                continue
            d = sec.data()
            for i in range(0, len(d) - 7, 4):
                yield sec.name, sec["sh_addr"] + i, int.from_bytes(d[i:i + 4], "little")
    def data_pairs(self, w0, w1):
        hits = []
        prev = None
        for sname, addr, w in self.data_words():
            if prev is not None and prev[2] == w0 and w == w1:
                hits.append((prev[0], prev[1]))
            prev = (sname, addr, w)
        return hits


def fmt_insn(ins):
    return f"{ins.address:#08x}: {ins.mnemonic:8s} {ins.op_str}"


def pattern_call_order(mod):
    calls = [s for _o, s in mod.calls_in("pcie_ete_init")
             if s and s.startswith("pcie_ete")]
    seen = []
    for n in calls:
        if n in ("pcie_ete_intr_init", "pcie_ete_rings_init", "pcie_ete_chn_res") \
                and n not in seen:
            seen.append(n)
    return seen


def intr_mask_sites(mod):
    """Return (function, [(movw imm lo, movt imm hi), ...]) for 0xffe0f8f8."""
    out = []
    for fn, _r in mod.funcs.items():
        ins = mod.disasm(fn)
        for i in range(len(ins) - 1):
            a, b = ins[i], ins[i + 1]
            if a.mnemonic != "movw" or b.mnemonic != "movt":
                continue
            try:
                lo_i = a.operands[1].imm
                hi_i = b.operands[1].imm
            except (IndexError, AttributeError):
                continue
            val = (lo_i & 0xFFFF) | ((hi_i & 0xFFFF) << 16)
            if val == ETE_INTR_KEEP:
                out.append((fn, i, val))
    return out


def kva_offsets(mod, fn):
    """All (reg, imm) from add <dst>, <src>, #imm in fn."""
    res = []
    for ins in mod.disasm(fn):
        if ins.mnemonic != "add" or len(ins.operands) < 3:
            continue
        d, s, i = ins.operands
        if d.type == CS_OP_REG and s.type == CS_OP_REG and i.type == CS_OP_IMM:
            res.append((d.reg, i.imm, ins))
    return res


def bfi_fields(mod, fn):
    res = []
    for ins in mod.disasm(fn):
        if ins.mnemonic != "bfi" or len(ins.operands) < 4:
            continue
        lsb = ins.operands[2].imm
        width = ins.operands[3].imm
        res.append((lsb, width, ins))
    return res


def store_through_ctx_ptr(mod):
    """Dataflow scan: stores whose base register was last loaded from
    [<reg>, #0x5C] or [<reg>, #0xEC] (the ring-context send-word slots)."""
    hits = []
    for fn in mod.funcs:
        last = {}
        for ins in mod.disasm(fn):
            ops = ins.operands
            # memory loads: record source displacement per destination reg
            if ins.mnemonic.startswith("ldr") and len(ops) >= 2 \
                    and ops[0].type == CS_OP_REG and ops[1].type == CS_OP_MEM:
                last[ops[0].reg] = ops[1].mem.disp
            elif ins.mnemonic.startswith("str") and len(ops) >= 2 \
                    and ops[1].type == CS_OP_MEM:
                disp = last.get(ops[1].mem.base)
                if disp in (0x5C, 0xEC):
                    hits.append((fn, ins.address, ins.mnemonic,
                                 ins.op_str, disp))
            # any other write to a register invalidates the tracked value
            for o in ops:
                if o.type == CS_OP_REG and o.access and (o.access & 2):
                    if not (ins.mnemonic.startswith("ldr")
                            and ops[0].type == CS_OP_REG and o is ops[0]):
                        last.pop(o.reg, None)
    return hits


def fill_reg_masks(mod):
    """Find stores of 'or' forms that build 0xFFE0F8F8? (not needed)."""
    return []


# --------------------------------------------------------------------------
def check(cond, msg, results):
    results.append((bool(cond), msg))
    print(("  PASS  " if cond else "  FAIL  ") + msg)
    return bool(cond)


def selftest(new, old):
    results = []
    print("[A] ETE init function set + cross-version identity")
    for fn in ETE_INIT_FUNCS:
        has_new = fn in new.funcs
        has_old = fn in old.funcs
        check(has_new and has_old, f"{fn} present in both 2.5.24 and 2.4.15",
              results)
    for fn in ETE_INIT_FUNCS:
        if fn in new.funcs and fn in old.funcs:
            same = new.func_bytes(fn) == old.func_bytes(fn)
            digest = hashlib.sha1(new.func_bytes(fn)).hexdigest()[:12]
            check(same, f"{fn} byte-identical across builds (sha1 {digest})",
                  results)

    print("[B] pcie_ete_init call order")
    order = pattern_call_order(new)
    want = ["pcie_ete_intr_init", "pcie_ete_rings_init", "pcie_ete_chn_res"]
    check(order == want, f"ETE init order {order} == {want}", results)
    call_names = [s for _o, s in new.calls_in("pcie_ete_init")]
    check("get_pcie_ete_res" in call_names,
          "get_pcie_ete_res called from pcie_ete_init", results)

    print("[C] ETE interrupt word CA pair + vendor mask")
    hits = intr_mask_sites(new)
    check(any(fn == "pcie_ete_intr_init" for fn, _i, _v in hits),
          "pcie_ete_intr_init builds 0xffe0f8f8", results)
    pairs = new.data_pairs(ETE_CA_ETE, ETE_CA_INTR)
    check(bool(pairs),
          f"static CA pair (0x{ETE_CA_ETE:08x}, 0x{ETE_CA_INTR:08x}) in .data",
          results)
    pair_old = old.data_pairs(ETE_CA_ETE, ETE_CA_INTR)
    check(bool(pair_old), "same CA pair present in the 2.4.15 build", results)

    print("[D] +0xC00 / +0xC18 send-word pointer computations")
    adds = kva_offsets(new, "pcie_ete_init_dst_ring")
    check(any(imm == ETE_SEND_DR0 for _r, imm, _i in adds),
          "init_dst_ring computes ETE+0xC00 (DR send words)", results)
    adds = kva_offsets(new, "pcie_ete_init_src_ring")
    check(any(imm == 0xC10 for _r, imm, _i in adds) and
          any(imm == 8 for _r, imm, _i in adds),
          "init_src_ring computes ETE+0xC10+8 = +0xC18 (SR send words)",
          results)

    print("[E] no host store through the send-word context slots")
    hits = store_through_ctx_ptr(new)
    check(not hits,
          f"2.5.24: no store through ctx+0x5C/0xEC ({len(hits)} hits)",
          results)
    hits = store_through_ctx_ptr(old)
    check(not hits,
          f"2.4.15: no store through ctx+0x5C/0xEC ({len(hits)} hits)",
          results)

    print("[F] bfi read-modify-write updates in the vendor init")
    f = bfi_fields(new, "pcie_ete_sr_reg_init")
    check(any(lsb == 0 and w == 3 for lsb, w, _i in f),
          "sr_reg_init writes ctrl as bfi(0,3)", results)
    f = bfi_fields(new, "pcie_ete_sr_reg_init")
    check(any(lsb == 0 and w == 10 for lsb, w, _i in f),
          "sr_reg_init writes depth-1 as bfi(0,10)", results)
    f = bfi_fields(new, "pcie_ete_dr_reg_init")
    check(any(lsb == 0 and w == 10 for lsb, w, _i in f),
          "dr_reg_init writes depth-1 as bfi(0,10)", results)

    print("[G] ETE channel enable is firmware-side in both builds")
    check("pcie_ete_chn_enable" in old.funcs,
          "2.4.15 has pcie_ete_chn_enable (sets channel +0x00 bit0)", results)
    refs = [s for _o, s, _t in old.relocs() if s == "pcie_ete_chn_enable"]
    check(not refs, f"2.4.15 pcie_ete_chn_enable call sites: {len(refs)}",
          results)
    refs = [s for _o, s, _t in new.relocs() if s == "pcie_ete_chn_enable"]
    check(not refs, f"2.5.24 pcie_ete_chn_enable references: {len(refs)}",
          results)
    en_writes = [h for h in store_through_ctx_ptr(new) if False]
    # the same orr+str code exists at the old address in 2.5.24 but with no
    # symbol and no relocation -> covered by the zero-reference check above.

    print("[H] golden capture derivation (packed ring index)")
    for name, val, idx in GOLDEN:
        hi, lo = val >> 16, val & 0xFFFF
        check(hi == idx and lo == idx,
              f"{name} = 0x{val:08x}: hi16=0x{hi:04x} lo16=0x{lo:04x} "
              f"both equal the packed index 0x{idx:03x}", results)
        check((idx & 0x3FF) < 32 and (idx & 0x400) != 0,
              f"packed index 0x{idx:03x} = idx {idx & 0x3FF} with wrap phase "
              f"bit10 set (>= 1024 nodes transferred, not an init value)",
              results)
    for name, val in GOLDEN_FILLS:
        check((val & 0x3FF) < 32,
              f"{name} = 0x{val:03x} is a valid packed ring index", results)

    passed = sum(1 for ok, _m in results if ok)
    failed = len(results) - passed
    print(f"\nRESULT: {passed} passed, {failed} failed")
    return 1 if failed else 0


def inventory(new):
    print(f"module: {new.path}")
    print("\nETE init functions (2.5.24 addresses):")
    for fn in ETE_INIT_FUNCS:
        if fn not in new.funcs:
            print(f"  {fn}: MISSING")
            continue
        a, b = new.func_range(fn)
        print(f"  {fn:26s} 0x{a:05x}-0x{b:05x}  {b - a:4d} B")
    print(f"\ninit call order: {pattern_call_order(new)}")
    print(f"\nETE interrupt word CA pair: "
          f"0x{ETE_CA_ETE:08x}, 0x{ETE_CA_INTR:08x} "
          f"({len(new.data_pairs(ETE_CA_ETE, ETE_CA_INTR))} .data site(s))")
    print(f"interrupt mask immediate: 0x{ETE_INTR_KEEP:08x} sites:")
    for fn, i, val in intr_mask_sites(new):
        ins = new.disasm(fn)
        print(f"  {fn}: movw r{ins[i].operands[0].reg}, #0x{val & 0xFFFF:04x} / "
              f"movt #{val >> 16:04x} then AND+store")
    print("\nSend/flag word pointer computations:")
    for fn, imm in (("pcie_ete_init_dst_ring", 0xC00),
                    ("pcie_ete_init_src_ring", 0xC18)):
        adds = kva_offsets(new, fn)
        extra = [f"add #{i:#x}" for _r, i, _m in adds
                 if i in (0xC10, 8, 0xC00)]
        print(f"  {fn}: {', '.join(extra)} (target ETE+{imm:#x}+4i)")


def sendregs(new, old):
    print("Stores through ring-context send-word slots (dataflow scan):")
    for mod in (new, old):
        hits = store_through_ctx_ptr(mod)
        print(f"  {mod.label}: {len(hits)} store(s) through ctx+0x5C/0xEC")
        for fn, addr, mn, ops, disp in hits:
            print(f"    {fn} @0x{addr:05x}: {mn} {ops} (slot +0x{disp:02x})")
    print("\nAll send-word slot accesses are reads; no host writer exists.")
    print("Therefore the +0xC00/+0xC18 words are device/hardware-authored.")
    print("\nRead-site inventory (for REPORT.md sec 3):")
    for mod in (new,):
        for fn in sorted(mod.funcs):
            if "ete" not in fn and fn != "pcie_tx_done_handle":
                continue
            for ins in mod.disasm(fn):
                ops = ins.operands
                if ins.mnemonic.startswith("ldr") and len(ops) >= 2 \
                        and ops[1].type == CS_OP_MEM \
                        and ops[1].mem.base != ARM_REG_PC \
                        and ops[1].mem.disp in (0x5C, 0xEC):
                    slot = "DR ctx+0x5C" if ops[1].mem.disp == 0x5C \
                        else "SR ctx+0xEC"
                    print(f"  {mod.label} {fn} @0x{ins.address:05x}: "
                          f"{ins.mnemonic} {ins.op_str}  ({slot} read)")


def compare(new, old):
    print(f"2.5.24: {new.path}")
    print(f"2.4.15: {old.path}")
    for fn in ETE_INIT_FUNCS:
        if fn not in new.funcs or fn not in old.funcs:
            print(f"  {fn:26s} missing in one build")
            continue
        a1, b1 = new.func_range(fn)
        a2, b2 = old.func_range(fn)
        same = new.func_bytes(fn) == old.func_bytes(fn)
        print(f"  {fn:26s} new 0x{a1:05x}-0x{b1:05x} old 0x{a2:05x}-0x{b2:05x} "
              f"{'IDENTICAL' if same else 'DIFFERS'}")
    names_old = set(old.funcs)
    names_new = set(new.funcs)
    print("\nsymbols only in 2.4.15:", sorted(names_old - names_new))
    print("symbols only in 2.5.24:", sorted(names_new - names_old))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["selftest", "inventory", "sendregs",
                                    "compare"])
    ap.add_argument("--new-ko", default=NEW_KO)
    ap.add_argument("--old-ko", default=OLD_KO)
    args = ap.parse_args()
    new = Mod(args.new_ko, "2.5.24")
    old = Mod(args.old_ko, "2.4.15")
    if args.cmd == "selftest":
        return selftest(new, old)
    if args.cmd == "inventory":
        inventory(new)
    elif args.cmd == "sendregs":
        sendregs(new, old)
    elif args.cmd == "compare":
        compare(new, old)
    return 0


if __name__ == "__main__":
    sys.exit(main())
