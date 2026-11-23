#!/usr/bin/env python3
"""fwdis.py - Hi5622V100 firmware image analyzer (host-only, image-only).

The image is the local blob FIRMWARE.bin (928,920 B). It is NEVER copied or
committed; this tool only reads it in place. Address conventions (established by
prior lanes and re-checkable with this tool):

  CPU address   = file offset + 0x40000   (image link base 0x40000)
  file offset   = CPU address - 0x40000
  host BAR0 off = CPU + 0x6B8000          for the CPU-RAM region (0x00000..0x1C0000)
  host BAR0 off = 0x3B8000 + (CA - 0x40000000) for the device IO register block
  firmware load CA (host upload) = 0x1240000 + file offset

Usage:
  fwdis.py info
  fwdis.py dis <cpu> [nbytes] [arm|thumb]
  fwdis.py w <cpu> [count] [word|half|byte]
  fwdis.py lits <cpu> [count]                    # dump literal pool words
  fwdis.py findval <hexvalue> [...]              # find a 32-bit word anywhere
  fwdis.py refs <cpu-address> [...]              # find PC-relative literal loads of these values
  fwdis.py strings <file-range-start> <len>
  fwdis.py dict <file-start> <len>               # decode bytes as word dictionary
"""
import sys, struct

FW_PATH = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
CPU_BASE = 0x40000
BAR0_RAM = 0x6B8000
BAR0_IO = 0x3B8000

def load():
    with open(FW_PATH, "rb") as f:
        return f.read()

FW = None
def fw():
    global FW
    if FW is None:
        FW = load()
    return FW

def f2c(off): return off + CPU_BASE
def c2f(cpu): return cpu - CPU_BASE
def c2bar0(cpu):
    """host BAR0 offset for a CPU address. RAM region uses +0x6B8000; IO block
    0x40000000+ uses 0x3B8000-(0x40000000)."""
    if cpu >= 0x40000000:
        return 0x3B8000 + (cpu - 0x40000000)
    return cpu + BAR0_RAM

def rw(cpu, n=4):
    off = c2f(cpu)
    if off < 0 or off + n > len(fw()):
        raise SystemExit("address 0x%X outside image" % cpu)
    return fw()[off:off+n]

def word(cpu): return struct.unpack("<I", rw(cpu,4))[0]
def half(cpu): return struct.unpack("<H", rw(cpu,2))[0]
def byte(cpu): return struct.unpack("<B", rw(cpu,1))[0]

def dis(cpu, n=0x80, mode="thumb", detail=False):
    import capstone
    from capstone import arm as ca
    m = capstone.CS_MODE_THUMB if mode.startswith("t") else capstone.CS_MODE_ARM
    md = capstone.Cs(capstone.CS_ARCH_ARM, m)
    md.detail = True
    off = c2f(cpu)
    print("== disasm cpu 0x%05X..0x%05X file 0x%05X.. (%s) ==" % (cpu, cpu+n, off, mode))
    for ins in md.disasm(fw()[off:off+n], cpu):
        extra = ""
        if ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):
            for op in ins.operands:
                if op.type == ca.ARM_OP_IMM:
                    t = op.imm & 0xffffffff
                    extra = "  ; -> cpu 0x%05X file 0x%05X" % (t, c2f(t))
        print("  c:0x%05X f:0x%05X  %-9s %s%s" % (ins.address, c2f(ins.address), ins.mnemonic, ins.op_str, extra))

def findval(vals):
    vs = [int(v, 0) for v in vals]
    data = fw()
    for v in vs:
        pat = struct.pack("<I", v)
        homes = []
        start = 0
        while True:
            i = data.find(pat, start)
            if i < 0: break
            homes.append(i)
            start = i + 1
        print("value 0x%08X: %d occurrence(s)" % (v, len(homes)))
        for h in homes:
            print("   file 0x%05X  cpu 0x%05X" % (h, f2c(h) if h + CPU_BASE < 0x40000000 else -1))

def refs(vals, want_mode="both"):
    """Disassemble whole image in ARM and Thumb and report PC-relative literal
    loads whose target word equals one of VALS. Approximate (linear sweep)."""
    import capstone
    vs = set(int(v, 0) if isinstance(v, str) else v for v in vals)
    targets = []
    data = fw()
    for mode, cs_mode in (("arm", capstone.CS_MODE_ARM), ("thumb", capstone.CS_MODE_THUMB)):
        if want_mode != "both" and want_mode != mode: continue
        md = capstone.Cs(capstone.CS_ARCH_ARM, cs_mode)
        md.detail = True
        for ins in md.disasm(data, CPU_BASE):
            if ins.mnemonic.startswith("ldr") and ins.operands:
                op = ins.operands[0]
                if op.type == 4 and op.mem.base == capstone.ARM_REG_PC:
                    # capstone already resolves PC-relative? use imm target
                    tgt = ins.address + 4 + op.mem.disp if cs_mode == capstone.CS_MODE_ARM else (ins.address + 4) & ~3
                    # recompute properly below instead
            # robust: compute literal address when possible
            for op in ins.operands:
                if op.type == 4 and op.mem.base == capstone.ARM_REG_PC and not op.mem.index:
                    pc = ins.address + (8 if cs_mode == capstone.CS_MODE_ARM else 4)
                    a = (pc + op.mem.disp) & 0xffffffff
                    if cs_mode == capstone.CS_MODE_THUMB:
                        a = a & ~3
                    off = c2f(a)
                    if 0 <= off <= len(data)-4:
                        w = struct.unpack("<I", data[off:off+4])[0]
                        if w in vs:
                            targets.append((mode, ins.address, a, w))
    for mode, ia, la, w in targets:
        print("  [%s] insn cpu 0x%05X loads literal cpu 0x%05X = 0x%08X" % (mode, ia, la, w))

def _sweep(callback, mode="both"):
    """Linear-sweep the image in ARM and/or Thumb and call callback(ins, mode).
    NOTE: a linear sweep over mixed code/data produces false positives; use it to
    find candidates, then confirm by disassembling the candidate range."""
    import capstone
    from capstone import arm as ca
    data = fw()
    modes = []
    if mode in ("both", "arm"): modes.append(("arm", capstone.CS_MODE_ARM))
    if mode in ("both", "thumb"): modes.append(("thumb", capstone.CS_MODE_THUMB))
    for mname, cm in modes:
        # mixed code/data streams desync a linear sweep; sweep every legal
        # instruction alignment (ARM: 4; Thumb: 2) to stay in sync with real code
        step = 4 if mname == "arm" else 2
        for mis in range(0, step):
            md = capstone.Cs(capstone.CS_ARCH_ARM, cm)
            md.detail = True
            for ins in md.disasm(data[mis:], CPU_BASE + mis):
                callback(ins, mname, ca)

def _lit_load(ins, mname, ca):
    """Return (literal_cpu, value) if ins is a PC-relative load, else None."""
    for op in ins.operands:
        if op.type == ca.ARM_OP_MEM and op.mem.base == ca.ARM_REG_PC:
            pc = ins.address + (8 if mname == "arm" else 4)
            a = (pc + op.mem.disp)
            if mname == "thumb": a &= ~3
            off = c2f(a)
            if 0 <= off <= len(fw()) - 4:
                import struct
                return a, struct.unpack("<I", fw()[off:off+4])[0]
    return None

def litrefs(vals, window=10):
    """Pattern-scan the raw image for LDR-literal instructions (ARM and Thumb,
    16-bit and Thumb-2) whose loaded word equals one of VALS. Robust against
    mixed code/data streams (no linear disassembly needed)."""
    import struct
    vs = set(int(v, 0) if isinstance(v, str) else v for v in vals)
    data = fw()
    hits = []
    for off in range(0, len(data) - 4):
        # Thumb-1: 01001 Rt imm8 -> LDR (literal)
        h = data[off] | (data[off+1] << 8)
        if (h >> 11) == 0b01001:
            cpu = f2c(off)
            tgt = ((cpu + 4) & ~3) + ((h & 0xff) << 2)
            to = c2f(tgt)
            if 0 <= to <= len(data) - 4:
                v = struct.unpack("<I", data[to:to+4])[0]
                if v in vs: hits.append((v, "thumb16", cpu, tgt))
        # Thumb-2: first halfword 11111000 11011111 = 0xF8DF (LDR.W Rt,[PC,#imm12])
        if h == 0xF8DF and off + 4 <= len(data):
            h2 = data[off+2] | (data[off+3] << 8)
            rt = (h2 >> 12) & 0xf; imm = h2 & 0xfff
            cpu = f2c(off)
            tgt = ((cpu + 4) & ~3) + imm
            to = c2f(tgt)
            if 0 <= to <= len(data) - 4:
                v = struct.unpack("<I", data[to:to+4])[0]
                if v in vs: hits.append((v, "thumb32", cpu, tgt))
        # ARM: LDR (literal) cond=1110, base=PC: 0xE59Fxxxx / 0xE51Fxxxx
        if off % 4 == 0:
            w = struct.unpack("<I", data[off:off+4])[0]
            if (w & 0x0F7F0000) == 0x051F0000 and (w >> 28) == 0xE:
                imm = w & 0xfff; up = (w >> 23) & 1
                cpu = f2c(off)
                tgt = cpu + 8 + (imm if up else -imm)
                to = c2f(tgt)
                if 0 <= to <= len(data) - 4:
                    v = struct.unpack("<I", data[to:to+4])[0]
                    if v in vs: hits.append((v, "arm", cpu, tgt))
    return hits

def ctx(vals, n=10, mode="both", window=8):
    """For each 32-bit value in VALS, find PC-relative literal loads of it and
    print the following instructions to show how the pointer is used."""
    import capstone
    for v in vals:
        v = int(v, 0)
        print("### uses of 0x%08X" % v)
        hs = [h for h in litrefs([v]) if mode == "both" or h[1].startswith(mode)]
        for val, mname, ia, la in hs:
            print("  --- load at cpu 0x%05X (%s) literal cpu 0x%05X" % (ia, mname, la))
            cm = capstone.CS_MODE_ARM if mname == "arm" else capstone.CS_MODE_THUMB
            md = capstone.Cs(capstone.CS_ARCH_ARM, cm); md.detail = True
            start = ia
            off = c2f(start)
            cnt = 0
            for ins in md.disasm(fw()[off:off + window*4 + 8], start):
                if cnt > window: break
                mark = " <== load" if ins.address == ia else ""
                print("    c:0x%05X f:0x%05X  %-9s %s%s" % (ins.address, c2f(ins.address), ins.mnemonic, ins.op_str, mark))
                cnt += 1

def _ctx_old(vals, n=10, mode="both", window=8):
    vs = set(int(v, 0) if isinstance(v, str) else v for v in vals)
    hits = []
    def cb(ins, mname, ca):
        r = _lit_load(ins, mname, ca)
        if r and r[1] in vs:
            hits.append((r[1], mname, ins.address, r[0]))
    _sweep(cb, mode)
    import capstone
    from capstone import arm as ca
    for v in vs:
        print("### uses of 0x%08X" % v)
        for val, mname, ia, la in hits:
            if val != v: continue
            print("  --- load at cpu 0x%05X (%s) literal 0x%05X" % (ia, mname, la))
            cm = capstone.CS_MODE_ARM if mname == "arm" else capstone.CS_MODE_THUMB
            md = capstone.Cs(capstone.CS_ARCH_ARM, cm); md.detail = True
            start = max(CPU_BASE, ia - window*4)
            off = c2f(start)
            cnt = 0
            for ins in md.disasm(fw()[off:off + (window*2)*4 + 8], start):
                if cnt > 2*window: break
                mark = " <== load" if ins.address == ia else ""
                print("    c:0x%05X f:0x%05X  %-9s %s%s" % (ins.address, c2f(ins.address), ins.mnemonic, ins.op_str, mark))
                cnt += 1

def movimm(target):
    """Scan both modes for MOVW/MOVT pairs that build TARGET, and for ADD/SUB from
    a PC-relative source. Print hits (candidates only)."""
    import capstone
    from capstone import arm as ca
    tgt = int(target, 0)
    hi = (tgt >> 16) & 0xffff; lo = tgt & 0xffff
    def cb(ins, mname, ca):
        if ins.mnemonic in ("movw", "movw.w") and ins.operands and ins.operands[0].type == ca.ARM_OP_IMM:
            pass
    # simpler: look at op_str text
    def cb2(ins, mname, ca):
        if "movw" in ins.mnemonic and hex(lo) in ins.op_str:
            print("  [%s] cpu 0x%05X: %s %s (low half)" % (mname, ins.address, ins.mnemonic, ins.op_str))
        if "movt" in ins.mnemonic and hex(hi) in ins.op_str:
            print("  [%s] cpu 0x%05X: %s %s (high half)" % (mname, ins.address, ins.mnemonic, ins.op_str))
    _sweep(cb2)

def strings(start_file, n):
    data = fw()[start_file:start_file+n]
    s = ""
    for i, b in enumerate(data):
        if 32 <= b < 127:
            s += chr(b)
        else:
            if len(s) >= 4:
                print("  file 0x%05X: %s" % (start_file + i - len(s), s))
            s = ""
    if len(s) >= 4:
        print("  file 0x%05X: %s" % (start_file + len(data) - len(s), s))

def main():
    a = sys.argv[1:]
    if not a or a[0] == "info":
        d = fw()
        print("image: %s  %d bytes (0x%X)" % (FW_PATH, len(d), len(d)))
        print("cpu range: 0x%05X .. 0x%05X" % (CPU_BASE, CPU_BASE + len(d)))
        print("first words:", " ".join("0x%08X" % struct.unpack("<I", d[i:i+4])[0] for i in range(0, 0x68, 4)))
        return
    cmd = a[0]
    if cmd == "dis":
        cpu = int(a[1], 0)
        n = int(a[2], 0) if len(a) > 2 else 0x80
        mode = a[3] if len(a) > 3 else "thumb"
        dis(cpu, n, mode)
    elif cmd == "w":
        cpu = int(a[1], 0); n = int(a[2], 0) if len(a) > 2 else 8
        kind = a[3] if len(a) > 3 else "word"
        for i in range(n):
            c = cpu + i*4
            print("  c:0x%05X f:0x%05X  = 0x%08X" % (c, c2f(c), word(c)))
    elif cmd == "findval":
        findval(a[1:])
    elif cmd == "refs":
        refs(a[1:])
    elif cmd == "bar0":
        for x in a[1:]:
            cpu = int(x, 0)
            print("  cpu 0x%05X  -> BAR0 offset 0x%05X" % (cpu, c2bar0(cpu)))
    elif cmd == "strings":
        strings(int(a[1], 0), int(a[2], 0))
    elif cmd == "ctx":
        mode = "both"
        vals = []
        for x in a[1:]:
            if x in ("arm", "thumb", "both"): mode = x
            else: vals.append(x)
        ctx(vals, mode=mode)
    elif cmd == "movimm":
        movimm(a[1])
    else:
        print(__doc__)

if __name__ == "__main__":
    main()
