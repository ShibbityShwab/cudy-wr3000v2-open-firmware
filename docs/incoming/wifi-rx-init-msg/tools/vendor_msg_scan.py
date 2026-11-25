#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""vendor_msg_scan.py - reproducible extraction of the vendor module's H2D
message surface (lane rx-init-decode/task-170).

  (1) config-event senders: every hmac_config_send_event / hdpp_config_send_event
      call site, with the u16 key (r1) and payload length (r2) recovered by a
      backward register scan; and
  (2) direct builders: every function that calls hcc_msg_tx / hcc_msg_tx_to_core
      / hmac_hcc_msg_tx_send, with the immediate id stored at header+6.

Reads a LOCAL vendor module only (never copies bytes into the repo).  Requires
the project ARM objdump (CROSS_OBJDUMP env) or arm-none-linux-gnueabihf-objdump
on PATH.  Usage:
    vendor_msg_scan.py --ko /path/hi5622v100_wifi.ko [--objdump PATH]
Prints markdown tables to stdout (the committed TABLES-*.md are this output).
"""
import argparse, os, re, subprocess, sys

def find_objdump(explicit):
    if explicit:
        return explicit
    env = os.environ.get("CROSS_OBJDUMP")
    if env:
        return env
    for cand in (
        os.path.join(os.path.dirname(__file__), "../../../../build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump"),
        "arm-none-linux-gnueabihf-objdump",
    ):
        cand = os.path.normpath(cand)
        if os.path.isabs(cand) and os.path.exists(cand):
            return cand
        from shutil import which
        if which(cand):
            return cand
    sys.exit("error: arm objdump not found; set --objdump or CROSS_OBJDUMP")

FUNC = re.compile(r"^([0-9a-f]{8}) <(.+)>:\s*$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+(\S+)\s*(.*)$")
SEC = re.compile(r"^Disassembly of section (.+):\s*$")
SYM = re.compile(r"<([^>+]+)(?:\+0x[0-9a-f]+)?>")
IMM = re.compile(r"#(0x[0-9a-fA-F]+|\d+)")

def parse(dis):
    section, funcs, cur = "?", [], None
    lits = {}
    for ln in dis.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+\.word\s+(0x[0-9a-fA-F]+|\d+)", ln)
        if m:
            lits[int(m.group(1), 16)] = int(m.group(2), 0)
    for ln in dis.splitlines():
        m = SEC.match(ln)
        if m:
            section, cur = m.group(1), None
            continue
        m = FUNC.match(ln)
        if m:
            cur = {"name": m.group(2), "section": section, "insns": []}
            funcs.append(cur)
            continue
        m = INSN.match(ln)
        if m and cur is not None:
            cur["insns"].append((int(m.group(1), 16), m.group(3), m.group(4).strip()))
    return funcs, lits

def imm(ops):
    m = IMM.search(ops)
    return int(m.group(1), 0) if m else None

LITS = None

def backward(fn, idx, nback=60):
    vals, insns = {}, fn["insns"]
    for j in range(idx - 1, max(0, idx - nback) - 1, -1):
        addr, mnem, ops = insns[j]
        parts = [p.strip() for p in ops.split(",")] if ops else []
        def put(reg, kind, val):
            if reg not in vals:
                vals[reg] = (kind, val)
        if mnem in ("mov", "movs", "movw", "movt") and len(parts) >= 2:
            rd, v = parts[0], imm(parts[1])
            if v is None:
                put(rd, "reg", parts[1])
            elif mnem == "movt" and rd in vals and vals[rd][0] == "imm16":
                put(rd, "imm", (v << 16) | vals[rd][1])
            elif mnem == "movw":
                put(rd, "imm16", v)
            else:
                put(rd, "imm", v)
        elif mnem in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh") and len(parts) >= 2:
            m2 = re.search(r"\[pc, #(0x[0-9a-fA-F]+|\d+)\]", parts[1])
            if m2 and LITS is not None:
                put(parts[0], "imm", LITS.get(addr + 8 + int(m2.group(1), 0)))
            else:
                put(parts[0], "mem", parts[1])
        elif mnem == "add" and len(parts) >= 3:
            v = imm(parts[2])
            if v is not None and parts[1] in vals and isinstance(vals[parts[1]][1], int):
                put(parts[0], "imm", vals[parts[1]][1] + v)
            else:
                put(parts[0], "reg", parts[1])
        elif parts and re.fullmatch(r"(r\d+|ip|sb|sl|fp|lr)", parts[0]):
            put(parts[0], "reg", parts[1] if len(parts) > 1 else "?")
    out = {}
    for reg in ("r0", "r1", "r2", "r3"):
        if reg in vals and vals[reg][0] in ("imm", "imm16"):
            out[reg] = vals[reg][1]
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ko", required=True)
    ap.add_argument("--objdump")
    a = ap.parse_args()
    od = find_objdump(a.objdump)
    dis = subprocess.run([od, "-dr", a.ko], capture_output=True, text=True, check=True).stdout
    funcs, lits = parse(dis)
    global LITS
    LITS = lits
    print("# TABLE - vendor config-event senders (g0 id1; key at hdr+20)", file=sys.stderr)
    print("# TABLE - vendor HCC message builders (hdr+6 = id)", file=sys.stderr)
    targets = {"hmac_config_send_event", "hdpp_config_send_event"}
    tx = {"hcc_msg_tx", "hcc_msg_tx_to_core", "hmac_hcc_msg_tx_send"}
    keys, rows, ncfg = {}, [], 0
    for fn in funcs:
        callees = set()
        for (addr, mnem, ops) in fn["insns"]:
            if mnem in ("bl", "blx"):
                ts = SYM.findall(ops)
                if ts:
                    callees.add(ts[-1])
        for i, (addr, mnem, ops) in enumerate(fn["insns"]):
            if mnem not in ("bl", "blx"):
                continue
            ts = SYM.findall(ops)
            if not ts or ts[-1] not in targets:
                continue
            ncfg += 1
            args = backward(fn, i)
            keys.setdefault(args.get("r1"), []).append((fn["name"], ts[-1], addr, args.get("r2")))
        if callees & tx:
            last, f6 = {}, 0
            for (addr, mnem, ops) in fn["insns"]:
                if mnem in ("mov", "movs", "movw") and "," in ops:
                    p = [x.strip() for x in ops.split(",")]
                    v = imm(p[1]) if len(p) > 1 else None
                    if v is not None:
                        last[p[0]] = v
                elif mnem == "strb":
                    m = re.match(r"(\w+),\s*\[(\w+)(?:,\s*#(0x[0-9a-fA-F]+|\d+))?\]", ops)
                    if m and m.group(1) in last and (int(m.group(3), 0) if m.group(3) else 0) == 6:
                        f6 = last[m.group(1)]
            rows.append((fn["name"], ",".join(sorted(callees & tx)), f6))
    print("## config-event senders", file=sys.stderr)
    for k in sorted([x for x in keys if x is not None]):
        v = keys[k]
        print("| 0x%04x | %s | %s | %s |" % (
            k, ", ".join(sorted({x[1] for x in v})),
            ", ".join(sorted({str(x[3]) for x in v if x[3] is not None})) or "?",
            "; ".join(sorted({x[0] + "@0x%x" % x[2] for x in v}))))
    for r in keys.get(None, []):
        print("dynamic-key:", r[0], r[1], "0x%x" % r[2])
    print("## builders (%d)" % len(rows))
    for (name, via, f6) in sorted(rows):
        print("| %s | %s | %s |" % (name, via, ("0x%02x" % f6) if f6 else "dyn"))
    print("totals: config calls %d, distinct keys %d, dynamic %d, builders %d"
          % (ncfg, len([k for k in keys if k is not None]), len(keys.get(None, [])), len(rows)))

if __name__ == "__main__":
    main()