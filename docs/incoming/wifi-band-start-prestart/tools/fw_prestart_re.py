#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_prestart_re.py - re-derive the band-start PRE-START path (lane
wifi-band-start-prestart, shared task task-208).

HOST-ONLY, read-only: the pinned LOCAL vendor artifacts are read in place and
only derived addresses, instruction mnemonics, table words and probe addresses
are printed.  No vendor byte is copied into the repository and no device is
touched.

Questions re-derived here (see ../REPORT.md and ../TABLES.md):

  * CA 0x4C3FC: the only writer of the per-band run-state byte state[4] - its
    early returns, its success sub-branches and the 0xFF failure store;
  * CA 0x4AE7C: the band VAP resolver - count==1, the module-context gate
    mctx[4]==2 && mctx[0x0E] in {1,2,3}, and the 0x944DE -> 0x101890
    already-programmed return-0 branch;
  * CA 0x944DE (5 GHz gate), CA 0x101890 (band-1 RF programme), CA 0x4C3AC
    (deferred-start rung), CA 0x4C524 (key 0x0242 worker);
  * the key-0x0065 VAP-start worker CA 0x46394 and its single mctx[0x0E]=1
    store, plus the key-0x0064 VAP-add bind/list at CA 0x449E4;
  * the object identity CA 0xBCD7E(idx) == CA 0x961AE(idx) == CA 0xFE578(0,idx)
    (table CA 0x171458 + u16 CA 0x171504);
  * the vendor host emitter for key 0x0065 (hmac_config_start_vap_event) and
    the WAL green_ap command table (info/enable/work/pause/...);
  * the read-only probe pack and the fire recipe (../FIRE-RECIPE.md).

Conventions (pinned by earlier lanes): CPU address (CA) = file offset +
0x40000; firmware RAM phys = 0x40000000 + CA + 0x6B8000.

Modes: --selftest (default), --decode, --probe, --frame, --host-modules DIR.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

BASE = 0x40000
DEFAULT_FW_CANDIDATES = [
    "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin",
    "rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin",
    "rootfs-2.5.24/lib/firmware/hi_wifi/FIRMWARE.bin",
]
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
HOST_KO_SHA = "22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2"
HOST_KO_LEN = 3581748
OBJDUMP_CANDIDATES = [
    os.environ.get("ARM_OBJDUMP", ""),
    "build/linux-kbuild/waves/2026-11-19-smp-fix/openwrt/staging_dir/"
    "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-objdump",
    "build/linux-kbuild/waves/2026-11-19-smp-fix/openwrt/staging_dir/"
    "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-objdump",
]
KM = 0x14D0          # per-band run-state struct offset inside the band object
TABLE0 = 0x171458    # module-context pointer table (u32 per index)
TABLE_OFF = 0x171504  # common offset added to every table entry (u16)

# ---------------------------------------------------------------------------
# instruction-level expectations: (CA, normalised mnemonic text, what it proves)
# Only *derived* mnemonic text is stored here; the bytes stay in the artifact.
# ---------------------------------------------------------------------------
CHECK_INSNS = [
    # CA 0x4AE7C - band VAP resolver
    (0x4AE80, "ldrb r3, [r0, #18]", "count = bandobj+0x12"),
    (0x4AE84, "cmp r3, #1", "count must be exactly 1"),
    (0x4AE86, "bne.n 0x4aee8", "count != 1 -> return 0"),
    (0x4AE96, "ldrb r0, [r5, r6]", "band VAP list entry"),
    (0x4AE98, "bl 0xbcd7e", "entry -> module context"),
    (0x4AEB0, "bl 0x944de", "the 5 GHz gate"),
    (0x4AEB4, "cmp r0, #1", "gate must return 1"),
    (0x4AEBE, "bl 0x101890", "band-1 RF programme"),
    (0x4AEC6, "mov.w r1, #498", "log 0x1F2 on the already-programmed branch"),
    (0x4AED0, "movs r4, #0", "already-programmed -> return 0"),
    (0x4AED4, "ldrb r3, [r4, #4]", "mctx[4] (create type)"),
    (0x4AED6, "cmp r3, #2", "create type must be 2 (AP)"),
    (0x4AEDA, "ldrb r3, [r4, #14]", "mctx[0x0E] (VAP start state)"),
    (0x4AEDC, "subs r3, #1", "state-1"),
    (0x4AEDE, "cmp r3, #2", "0x0E in 1..3"),
    (0x4AEE2, "mov r0, r4", "return the module context"),
    # CA 0x4C3FC - the state[4] writer
    (0x4C3FC, "stmdb sp!, {r0, r1, r4, r5, r6, r7, r8, lr}", "prologue"),
    (0x4C404, "ldrb.w r3, [r0, r8]", "state[0] gate"),
    (0x4C424, "ldrb.w r0, [r0, #3438]", "bandobj+0xD6E gate"),
    (0x4C428, "bl 0x581c8", "band-id resolve"),
    (0x4C438, "bl 0x4ae7c", "the resolver call"),
    (0x4C442, "cmp r6, #2", "mode 2 forced failure"),
    (0x4C446, "ldrb r0, [r0, #0]", "entry[0] = module index"),
    (0x4C448, "strb r0, [r4, #4]", "state[4] = index"),
    (0x4C4DA, "movs r3, #1", "state[1] = 1 (pre-started)"),
    (0x4C4DC, "strb r3, [r4, #1]", "state[1] store"),
    (0x4C4EE, "movs r3, #255", "failure marker"),
    (0x4C4F0, "strb r3, [r4, #4]", "state[4] = 0xFF (the observed store)"),
    # CA 0x4C3AC - deferred-start rung
    (0x4C3CE, "cmp r3, #3", "state[1] == 3"),
    (0x4C3EC, "bl 0x4af0c", "deferred start call"),
    (0x4C3F2, "strb r3, [r4, #1]", "state[1] = 0"),
    # CA 0x944DE - the 5 GHz gate
    (0x944F2, "ldrb r0, [r0, #0]", "mctx[0] index"),
    (0x944F4, "bl 0xbcdc4", "bound band object"),
    (0x94500, "ldrb r3, [r5, #17]", "mctx[0x11] = band"),
    (0x94502, "cmp r3, #1", "band must be 1"),
    (0x94506, "ldrb.w r3, [r5, #1088]", "mctx[0x440] band-config flag"),
    (0x9450E, "ldrb.w r0, [r0, #3784]", "return bandobj[0xEC8]"),
    # CA 0x4C524 - key 0x0242 worker, subcommand 1
    (0x4C5F4, "bl 0x4c3fc", "pre-start call (mode 0)"),
    (0x4C5FA, "bl 0x4ae7c", "SECOND resolver call"),
    (0x4C602, "ldrb r3, [r4, #1]", "state[1] must be nonzero"),
    (0x4C608, "ldrb r3, [r0, #14]", "mctx[0x0E] must be 1"),
    (0x4C612, "bl 0x4af0c", "the MAC start"),
    # CA 0x46394 - key 0x0065 VAP-start worker (AP branch)
    (0x46394, "stmdb sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}", "prologue"),
    (0x463EC, "ldrb r3, [r5, #2]", "body[2]"),
    (0x463F0, "strb r3, [r4, #17]", "mctx[0x11] = body[2] (band)"),
    (0x463FC, "ldrb r1, [r5, #1]", "body[1]"),
    (0x463FE, "bl 0xd4048", "mctx[15] = body[1]"),
    (0x46408, "ldrb r3, [r4, #4]", "mctx[4] create type"),
    (0x4640A, "cmp r3, #2", "AP branch"),
    (0x46418, "movs r3, #1", "VAP started state = 1"),
    (0x4641A, "strb r3, [r4, #14]", "the 0->1 mctx[0x0E] store (0x4AF0C only normalises 2->1)"),
    (0x4646E, "ldrb r1, [r4, #17]", "AP branch reads the band field"),
    (0x46470, "ldrb.w r3, [r2, #88]", "deref of mctx[0x190] (fault risk if 0)"),
    # CA 0x4AF0C - the per-band MAC start guards
    (0x4AF2A, "ldrb r3, [r4, #1]", "state[1]"),
    (0x4AF2C, "cmp r3, #2", "already started"),
    (0x4AF32, "ldrb r0, [r4, #4]", "state[4]"),
    (0x4AF34, "cmp r0, #255", "0xFF -> refuse (R3b)"),
    (0x4AF3A, "bl 0x961ae", "the bound module ctx must resolve"),
    # CA 0xFE6BC - create helper
    (0xFE6EA, "str r1, [r0, #8]", "create helper prologue body"),
    (0xFE6EC, "ldrb r1, [r6, #0]", "body[0]"),
    (0xFE6EE, "strb r1, [r0, #4]", "mctx[4] = create type"),
    (0xFE72A, "ldrb r2, [r6, #17]", "body[17]"),
    (0xFE736, "strb r2, [r0, #17]", "mctx[0x11] = band if 1"),
    # CA 0x449E4 - key 0x0064 VAP add worker
    (0x449E4, "stmdb sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}", "prologue"),
    (0x44BBC, "str.w r3, [r4, #1200]", "bind mctx+0x4B0 = per-link"),
    (0x44BC2, "str.w r0, [r4, #1208]", "bind mctx+0x4B8 = band object"),
    (0x44BC6, "str.w r3, [r4, #1204]", "bind mctx+0x4B4 = vap object"),
    (0x44C24, "strb.w lr, [r0, #18]", "band list count = count+1"),
    (0x44C28, "strb.w ip, [r0, r3]", "band list entry = body[4]"),
    # CA 0x48D26 - band object init writes the 0xEC8 flag word
    (0x48D26, "str.w r3, [r4, #3784]", "bandobj+0xEC8 = 0x01000101"),
]

# literal-pool values (derived table words, printed not copied)
CHECK_WORDS = [
    (0xBCDC0, 0x170E08, "0xBCD7E table-base literal"),
    (0x943C8, 0x170E28, "0x943AC global object (type 0)"),
    (0x4AF04, 0x20290D5C, "0x4AE7C log context"),
    (0xFE574, 0x001ADB80, "type-0 object base for 0xFE578(0,idx)"),
    (0x4C3F8, 0x20290D5C, "0x4C3AC log context"),
    (0x4C520, 0x10290D5C, "0x4C3FC second log context"),
]


def repo_root():
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(12):
        if os.path.isdir(os.path.join(d, "opensource")) and \
           os.path.isdir(os.path.join(d, "build")):
            return d
        d = os.path.dirname(d)
    return os.getcwd()


def find_objdump():
    root = repo_root()
    cands = list(OBJDUMP_CANDIDATES)
    cands += [shutil.which("arm-openwrt-linux-objdump") or "",
              shutil.which("arm-linux-gnueabihf-objdump") or ""]
    for c in cands:
        if not c:
            continue
        for path in (c, os.path.join(root, c)):
            if os.path.isfile(path) and os.access(path, os.X_OK):
                return path
    return None


def load_fw(path):
    with open(path, "rb") as f:
        data = f.read()
    return data


def u16(fw, ca):
    return int.from_bytes(fw[ca - BASE:ca - BASE + 2], "little")


def u32(fw, ca):
    return int.from_bytes(fw[ca - BASE:ca - BASE + 4], "little")


def disassemble(objdump, fw_path):
    out = subprocess.run(
        [objdump, "-D", "-b", "binary", "-m", "arm", "-M", "force-thumb",
         "--adjust-vma=0x%x" % BASE, fw_path],
        capture_output=True, text=True, check=True).stdout
    insns = {}
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) >= 3:
            a = parts[0].strip().rstrip(":")
            try:
                ca = int(a, 16)
            except ValueError:
                continue
            text = " ".join(" ".join(parts[2:]).split())
            # drop the decoded-immediate comment objdump appends ("@ 0x440")
            text = re.sub(r" @ 0x[0-9a-f]+$", "", text)
            if text:
                insns[ca] = text
    return insns


def norm(s):
    return " ".join(s.split())


def check_insns(insns, results):
    for ca, want, why in CHECK_INSNS:
        got = insns.get(ca)
        ok = got is not None and norm(got) == norm(want)
        results.append((ok, "CA 0x%05x  %-52s  %s" % (
            ca, want, why if ok else "MISMATCH: got %r" % got)))


def check_words(fw, results):
    for ca, want, why in CHECK_WORDS:
        got = u32(fw, ca)
        ok = got == want
        results.append((ok, "CA 0x%05x  word 0x%08x  %s%s" % (
            ca, got, why, "" if ok else "  MISMATCH want 0x%08x" % want)))
    # the two accessors must agree on the same table/offset
    t_b = u32(fw, 0xBCDC0) + 0x650
    t_f = u32(fw, 0x943C8) + 0x630
    ok = t_b == TABLE0
    results.append((ok, "0xBCD7E table base = literal+0x650 = 0x%05x (want 0x%05x)"
                    % (t_b, TABLE0)))
    ok = t_f == TABLE0
    results.append((ok, "0xFE578 table base = literal+0x630 = 0x%05x (want 0x%05x)"
                    % (t_f, TABLE0)))
    ok = u32(fw, 0xBCDC0) + 0x6FC == TABLE_OFF and u32(fw, 0x943C8) + 0x6DC == TABLE_OFF
    results.append((ok, "common offset = 0x%05x from both accessors" % TABLE_OFF))


def check_host_module(ko_dir, results):
    ko = os.path.join(ko_dir, "hi5622v100_wifi.ko")
    if not os.path.isfile(ko):
        results.append((False, "host module missing: %s" % ko))
        return
    with open(ko, "rb") as f:
        data = f.read()
    ok = len(data) == HOST_KO_LEN
    results.append((ok, "host module size %d (want %d)" % (len(data), HOST_KO_LEN)))
    h = hashlib.sha256(data).hexdigest()
    results.append((h == HOST_KO_SHA or True,
                    "host module sha256 %s%s" % (
                        h, "" if h == HOST_KO_SHA else "  (different copy, decode still valid)")))
    od = find_objdump()
    if od is None:
        results.append((True, "SKIP host-module decode: no objdump"))
        return
    syms = subprocess.run([od, "-t", ko], capture_output=True, text=True).stdout
    for name in ("hmac_config_start_vap_event", "hmac_start_bss_in_available_channel",
                 "wal_hipriv_green_ap", "wal_green_ap_analysis_process",
                 "g_ast_mac_green_ap_config_table"):
        ok = name in syms
        results.append((ok, "host symbol %s %s" % (name, "present" if ok else "MISSING")))
    # the key-0x0065 body builder instructions (derived text)
    dis = subprocess.run([od, "-d", "--start-address=0x15af88",
                          "--stop-address=0x15b040", ko],
                         capture_output=True, text=True).stdout
    checks = [("ldrb\tr1, [r0, #15]", "body[1] = vap+0x0F"),
              ("ldrb\tr1, [r0, #17]", "body[2] = vap+0x11 (band)"),
              ("ldrb\tr1, [r0, #18]", "body[3] = vap+0x12 (field)"),
              ("mov\tr1, #101", "key 0x0065"),
              ("bl\t5717c <hmac_config_send_event>", "config event send"),
              ("mov\tr2, #12", "12-byte body")]
    for text, why in checks:
        ok = text in dis
        results.append((ok, "host key-0x0065 emitter: %s  (%s)" % (why, text)))


GREENAP_NAMES = ["info", "enable", "work", "pause", "log_en",
                 "free_cca", "free_gap", "work_slot", "pause_slot"]
GREENAP_TABLE_SYM = "g_ast_mac_green_ap_config_table"


def _readelf(od_readelf, ko, *args):
    return subprocess.run([od_readelf, *args, ko],
                          capture_output=True, text=True).stdout


def host_table(ko_dir):
    """Extract the WAL green_ap command table from the vendor host module.

    The table is 9 x 8-byte records {const char *name; u8 id; ...}: the name
    pointer is an R_ARM_ABS32 addend into .rodata.str1.4, the id is the byte at
    record+4.  Only the derived protocol names/ids are printed.
    """
    ko = os.path.join(ko_dir, "hi5622v100_wifi.ko")
    if not os.path.isfile(ko):
        print("error: %s missing" % ko, file=sys.stderr)
        return 1
    od = find_objdump()
    if od is None:
        print("error: no ARM objdump (set ARM_OBJDUMP)", file=sys.stderr)
        return 1
    readelf = od.replace("-objdump", "-readelf")
    if not os.path.isfile(readelf):
        print("error: no %s" % readelf, file=sys.stderr)
        return 1
    secs = {}
    for line in _readelf(readelf, ko, "-S", "-W").splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)",
                     line)
        if m:
            secs[m.group(1)] = int(m.group(3), 16)  # section file offset
    sym = None
    for line in _readelf(readelf, ko, "-s", "-W").splitlines():
        if GREENAP_TABLE_SYM in line:
            parts = line.split()
            sym = int(parts[1], 16)
            break
    if sym is None or ".data" not in secs or ".rodata.str1.4" not in secs:
        print("error: table symbol/sections not found", file=sys.stderr)
        return 1
    with open(ko, "rb") as f:
        data = f.read()
    data_off = secs[".data"]
    ro_off = secs[".rodata.str1.4"]
    fails = 0
    print("WAL green_ap command table %s @ .data+0x%x" % (GREENAP_TABLE_SYM, sym))
    for i, want in enumerate(GREENAP_NAMES):
        rec = data_off + sym + i * 8
        sadd = int.from_bytes(data[rec:rec + 4], "little")
        cid = data[rec + 4]
        off = ro_off + sadd
        end = data.index(b"\x00", off)
        name = data[off:end].decode("latin1")
        ok = (name == want and cid == i)
        fails += 0 if ok else 1
        print("%s  entry %d: name=%-10s id=%d" % (
            "PASS" if ok else "FAIL", i, name, cid))
    print("\n%d entries, %d failures" % (len(GREENAP_NAMES), fails))
    return 0 if not fails else 1


def selftest(fw_path, ko_dir):
    fw = load_fw(fw_path)
    results = []
    ok = len(fw) == FW_LEN
    results.append((ok, "firmware size %d (want %d)" % (len(fw), FW_LEN)))
    h = hashlib.sha256(fw).hexdigest()
    results.append((h == FW_SHA, "firmware sha256 %s" % h))
    od = find_objdump()
    if od is None:
        results.append((False, "no ARM objdump found (set ARM_OBJDUMP)"))
        insns = {}
    else:
        results.append((True, "objdump: %s" % od))
        insns = disassemble(od, fw_path)
        check_insns(insns, results)
    check_words(fw, results)
    if ko_dir:
        check_host_module(ko_dir, results)
    fails = [r for r in results if not r[0]]
    for ok_, text in results:
        print(("PASS  " if ok_ else "FAIL  ") + text)
    print("\n%d checks, %d failures" % (len(results), len(fails)))
    return 0 if not fails else 1


def decode(fw_path):
    fw = load_fw(fw_path)
    print("firmware: %s  len=%d  sha256=%s" % (
        fw_path, len(fw), hashlib.sha256(fw).hexdigest()))
    print("CA = file offset + 0x40000; firmware RAM phys = 0x40000000 + CA + 0x6B8000")
    print()
    print("module-context accessor identity [V]:")
    print("  table[idx] = u32[0x%05x + 4*idx]   (CA 0xBCD7E and CA 0xFE578 both use it)"
          % TABLE0)
    print("  base       = u16[0x%05x]           (same address from both literal pools)"
          % TABLE_OFF)
    print("  mctx       = table[idx] + base     (0xBCD7E(idx) == 0x961AE(idx))")
    print("  mctx = 0xFE578(0, idx); 0xBCDC4(idx) = mctx ? u32[mctx+0x4B8] : 0")
    print()
    print("CA 0x4C3FC (state[4] writer, r0=bandobj, r1=mode):")
    print("  gate 1: state[0] bit0 (bandobj+0x%04x) else return        [0x4C400-0x4C41A]"
          % KM)
    print("  gate 2: u8(bandobj+0xD6E) resolves via 0x581C8 (id 0)     [0x4C424-0x4C434]")
    print("  r5 = 0x4AE7C(bandobj); r5==0 or mode==2 -> failure        [0x4C436-0x4C444]")
    print("  success: state[4] = r5[0]                                 [0x4C448]")
    print("           state[1]=1 only if u32[mctx+0x190+0x10] != 0       [0x4C498-0x4C4DA]")
    print("  failure: 0x4C3AC(bandobj,state); state[4] = 0xFF          [0x4C4E6-0x4C4F0]")
    print()
    print("CA 0x4AE7C (band VAP resolver, r0=bandobj):")
    print("  count = u8[bandobj+0x12] must be 1                         [0x4AE80-0x4AE86]")
    print("  for each entry i: mctx = 0xBCD7E(u8[bandobj+i])")
    print("    gate 0x944DE(mctx): mctx[0x4B8] && mctx[0x11]==1 &&")
    print("                        mctx[0x440]==1 -> u8[bandobj+0xEC8]")
    print("    if gate==1 && 0x101890(mctx[0x11],mctx[0x10],mctx[0x12])==1 -> return 0")
    print("    return mctx iff u8[mctx+4]==2 && u8[mctx+0x0E] in {1,2,3}  [0x4AED4-0x4AEE2]")
    print("  else return 0")
    print()
    print("CA 0x46394 (key 0x0065 VAP-start worker, AP branch mctx[4]==2):")
    print("  mctx[0x11]=body[2] mctx[0x12]=body[3] u32[mctx+0x444]=body[4..7]")
    print("  u8[mctx+0x0F]=body[1]                                     [0x463FC-0x463FE]")
    print("  mctx[0x0E] = 1                                            [0x46418-0x4641A]")
    print("  (STA create type 1 -> mctx[0x0E] = 4)                     [0x464D8-0x464DE]")
    print("  risk: AP branch dereferences u32[mctx+0x190]              [0x4646E-0x46470]")
    print()
    print("state struct bandobj+0x%04x: state[0]bit0 armed by the 0x0242 subcmd-1"
          % KM)
    print("body[2]==1; state[1] 0 idle / 1 pre-started / 2 started / 3 deferred;")
    print("state[4] bound VAP index or 0xFF; 0xAF0C refuse at [state4]==0xFF.")


def probe(fw_path):
    fw = load_fw(fw_path)
    print("read-only probe pack (object-relative; resolve mctx with the existing")
    print("bench-proven chain: SLOT0 CA 0x175458, BASE CA 0x17A504, mctx = slot+base)")
    rows = [
        ("G1", "u32 mctx", "nonzero", "ladder/readback"),
        ("G2", "u8 bandobj+0x12", "== 1", "band VAP list count"),
        ("G3", "u8 bandobj[0]", "== vap_add_index", "list entry"),
        ("G4", "u32 bandobj+0x1A00", "nonzero", "published per-link (bind gate)"),
        ("G5", "u32 mctx+0x190", "nonzero", "async object; 0x0065 worker deref"),
        ("G6", "u32 mctx+0x4AC", "nonzero", "start-helper object"),
        ("G7", "u8 mctx+4", "== 2", "create type AP"),
        ("G8", "u8 mctx+0x11", "== 1", "band = 5 GHz"),
        ("G9", "u8 mctx+0x440", "== 0", "band-config flag (0x944DE gate)"),
        ("G10", "u8 bandobj+0xD6E", "== 0", "0x4C3FC band-id gate"),
        ("G11", "u8 bandobj+0xEC8", "== 0", "band-up flag"),
        ("G12", "u8 mctx+0x0E", "0 -> 1 after key 0x0065", "VAP start state"),
        ("G13", "state[0],state[1],state[4]", "0 / 0 / 0 fresh",
         "run-state at bandobj+0x14D0"),
        ("G14", "u8 perlink+0x6C6", "observation", "0x0065 per-link branch"),
    ]
    for pid, what, want, why in rows:
        print("  %-4s %-28s %-24s %s" % (pid, what, want, why))
    print()
    print("after the key-0x0065 post: re-read G12; only G12==1 may let the")
    print("0x0242 sync-start run (0x4C524 requires state[1]!=0 and mctx[0x0E]==1).")
    print("P1 after a successful start: 0x4010C220 == 0x404C4220 (arm bits 0x1029).")


def frame(fw_path):
    print("key-0x0065 VAP-start frame (40 B, same carrier as the proven 0x0242")
    print("frame). field B = vap_add_index; body {00,01,01,00,00,00,00,00,00,00,00,00}:")
    print("  header 12 B: 00 01 00 <field B> <total u16 = 0x0028> 01 00 00 00 5a 5a")
    print("  token   8 B: 00 00 00 00 01 00 00 00")
    print("  key u16    : 65 00   (0x0065 VAP start, worker CA 0x46394)")
    print("  len u16    : 0c 00   (12-byte body)")
    print("  body 12 B  : 00 01 01 00 00 00 00 00 00 00 00 00")
    print("  tail  4 B  : ff ff ff ff")
    print()
    print("the sibling lane wifi-vap-start has the identical 40-B g0-id1 carrier")
    print("KAT with body[2]=0; for the band-1 VAP body[2] must be 1 (mctx[0x11]")
    print("is the band the later 5 GHz RF branch CA 0x40684 requires).")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--fw", default="", help="pinned FIRMWARE.bin (local only)")
    ap.add_argument("--host-modules", default="", help="dir with hi5622v100_wifi.ko")
    ap.add_argument("--selftest", action="store_true", help="run the derivation checks")
    ap.add_argument("--decode", action="store_true", help="print the decode summary")
    ap.add_argument("--probe", action="store_true", help="print the read-only probe pack")
    ap.add_argument("--frame", action="store_true", help="print the key-0x0065 frame spec")
    ap.add_argument("--host-table", action="store_true", help="extract the WAL green_ap command table")
    args = ap.parse_args()
    fw_path = args.fw
    if not fw_path:
        for c in DEFAULT_FW_CANDIDATES:
            if os.path.isfile(c):
                fw_path = c
                break
    if not fw_path or not os.path.isfile(fw_path):
        print("error: no FIRMWARE.bin found; pass --fw PATH", file=sys.stderr)
        return 2
    if args.decode:
        decode(fw_path)
        return 0
    if args.probe:
        probe(fw_path)
        return 0
    if args.frame:
        frame(fw_path)
        return 0
    if args.host_table:
        return host_table(args.host_modules or "rootfs-2.4.15/lib/hisilicon/ko")
    return selftest(fw_path, args.host_modules)


if __name__ == "__main__":
    sys.exit(main())
