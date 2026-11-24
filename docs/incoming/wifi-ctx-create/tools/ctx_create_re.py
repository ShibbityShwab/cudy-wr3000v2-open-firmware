#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ctx_create_re.py - verify the firmware's H2D context-create path for the TXQ sync.

Lane wifi-ctx-create (shared task task-101), host-only static RE. Reads the pinned LOCAL
vendor firmware blob in place, prints derived annotations only; it never copies vendor
bytes into the repository and never opens a device.

The path verified here:

  mailbox id 3 (SR announce) -> firmware message worker -> HCC group-0 receive table
  (CA 0x10CCFC, registered by CA 0x102866 from boot chain CA 0x48346)
    -> group-0 id-1 handler CA 0x43D58
    -> key table A CA 0x10C23C (217 x {u16 key; u16 pad; u32 worker}; key = u16 hdr+0x14)
    -> key 0x0064 -> worker CA 0x449E4
    -> data[1] = parent module index, data[4] = module index to create
    -> context-create helper CA 0xFE6BC, which allocates the per-context buffer and
       stores its pointer at ctx+0x190 (CA 0xFE7E4)
  (the same helper is also called by the firmware boot task at CA 0x48862, but with the
   fixed id 0x12 and a zeroed data block -> type 0 -> NO +0x190 allocation)

The TXQ config sync (device capture: group 0 id 1, hdr[3]=4, key 0x00d8) resolves through
the same table to worker CA 0x81038, which dereferences ctx+0x190 - hence the
create-before-sync ordering.

Subcommands:
  trace      the H2D handler -> key table -> worker -> create helper chain
  tables     key table A/B decode (key -> worker)
  readback   bench read-only pack (BAR0 = CPU + 0x6B8000) with expected values
  selftest   self-checking assertions (exit 0 on PASS)

CPU = file + 0x40000; host BAR0 of firmware RAM = CPU + 0x6B8000.
"""
import argparse
import hashlib
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_OP_IMM
except Exception:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python)")
    raise SystemExit(2)

BASE = 0x40000
DEFAULT_FW = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
BAR0_OFF = 0x6B8000

# ---- H2D registration / handler ----------------------------------------------------------
CPU_HCC_REGISTRAR = 0x102866
CPU_BOOT_REG_G0 = 0x48346
CPU_BOOT_REG_G2 = 0x4833C
CPU_G0_TABLE = 0x10CCFC
CPU_G2_TABLE = 0x10CC18
CPU_G0ID1_HANDLER = 0x43D58
CPU_MSG_HDR_FETCH = 0x409EA
CPU_CTX_LOOKUP = 0xFE578
CPU_LOOKUP_TAILCALL = 0x961AE
CPU_TABLE_A = 0x10C23C
CPU_TABLE_B = 0x103BF8
TAB_A_N = 217
TAB_B_N = 88
KEY_CREATE = 0x64
CPU_WORKER_CREATE = 0x449E4
KEY_TXQ_SYNC = 0xD8
CPU_WORKER_TXQ = 0x81038
CPU_WORKER_TXQ_STORE = 0x81040

# ---- user-manager global / module blocks ------------------------------------------------
CPU_G = 0x170E28
G_LOOKUP = 0x18C
G_COM = 0x6DC
G_REFCOUNT = 0x6C8
G_SINGLETON = 0x169E90
CPU_USER_MGR_INIT = 0x49204
CPU_PTR_BASE = 0x119758
CTX_STRIDE = 0xB40
CTX_COUNT = 19
CPU_CTX4 = CPU_PTR_BASE + 4 * CTX_STRIDE
CPU_G_CTX4 = CPU_G + 0x630 + 4 * 4
CPU_CTX4_190 = CPU_CTX4 + 0x190
CPU_G_COM = CPU_G + G_COM

# ---- context-create helper --------------------------------------------------------------
CPU_CREATE_HELPER = 0xFE6BC
CPU_CREATE_STORE = 0xFE7E4
CPU_ALLOC = 0xBF128
BOOT_CREATE_CALL = 0x48862
BOOT_CREATE_ID = 0x12
WORKER_CREATE_CALL = 0x44AAC
CPU_BOOT_DATA_MEMSET = 0x487EA

TH = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
TH.detail = True
AR = Cs(CS_ARCH_ARM, CS_MODE_ARM)


class Blob:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()

    def win(self, cpu, n):
        return self.d[cpu - BASE:cpu - BASE + n]

    def u16(self, cpu):
        return struct.unpack_from("<H", self.d, cpu - BASE)[0]

    def u32(self, cpu):
        return struct.unpack_from("<I", self.d, cpu - BASE)[0]

    def dis(self, cpu, n):
        return list(TH.disasm(self.win(cpu, n), cpu))

    def print_dis(self, cpu, n, mark=None):
        for i in self.dis(cpu, n):
            tag = "   <== " + mark[1] if mark and i.address == mark[0] else ""
            print("  %08x  %-10s %-9s %s%s" % (i.address, i.bytes.hex(), i.mnemonic,
                                              i.op_str, tag))


def table_decode(blob, base, count):
    out = []
    for k in range(count):
        a = base + 8 * k
        out.append((k, a, blob.u16(a), blob.u32(a + 4)))
    return out


def key_worker(blob, key):
    for name, base, n in (("A", CPU_TABLE_A, TAB_A_N), ("B", CPU_TABLE_B, TAB_B_N)):
        for k, a, kk, w in table_decode(blob, base, n):
            if kk == key:
                return name, k, a, w
    return None


def scan_callers(blob, target):
    out = []
    n = len(blob.d)
    for off in range(0, n - 4, 2):
        a = BASE + off
        ins = list(TH.disasm(blob.d[off:off + 4], a, count=1))
        if not ins:
            continue
        i = ins[0]
        if i.mnemonic in ("bl", "bl.w") and i.operands and i.operands[0].type == CS_OP_IMM:
            if i.operands[0].imm == target:
                out.append(a)
    return out


def cmd_trace(blob):
    print("== the H2D context-create path (all addresses CPU; file = CPU - 0x40000) ==")
    print()
    print("[1] HCC group-0 receive table CA 0x%06x (registered at CA 0x%06x via CA 0x%06x)"
          % (CPU_G0_TABLE, CPU_BOOT_REG_G0, CPU_HCC_REGISTRAR))
    print("    entry id 1 -> handler CA 0x%06x" % CPU_G0ID1_HANDLER)
    print()
    print("[2] group-0 id-1 handler CA 0x%06x (config-event demultiplexer)" % CPU_G0ID1_HANDLER)
    blob.print_dis(CPU_G0ID1_HANDLER, 0x28, mark=(0x43D76, "ctx = lookup(0, hdr[3])"))
    print("    key  = u16 hdr+0x14 (payload+8);  subkey = u8 hdr+0x16;  data = hdr+0x18")
    print("    table A base literal at CA 0x43E18 = 0x%06x" % blob.u32(0x43E18))
    print("    table B base literal at CA 0x43E1C = 0x%06x" % blob.u32(0x43E1C))
    print()
    kw = key_worker(blob, KEY_CREATE)
    kx = key_worker(blob, KEY_TXQ_SYNC)
    print("[3] key table A CA 0x%06x: 217 entries {u16 key; u16 pad; u32 worker}" % CPU_TABLE_A)
    if kw:
        print("    key 0x%04x -> table %s entry %d @CA 0x%06x -> worker CA 0x%06x"
              % (KEY_CREATE, kw[0], kw[1], kw[2], kw[3]))
    if kx:
        print("    key 0x%04x -> table %s entry %d @CA 0x%06x -> worker CA 0x%06x"
              % (KEY_TXQ_SYNC, kx[0], kx[1], kx[2], kx[3]))
    print()
    print("[4] create worker CA 0x%06x (key 0x%04x):" % (CPU_WORKER_CREATE, KEY_CREATE))
    blob.print_dis(CPU_WORKER_CREATE, 0x100, mark=(WORKER_CREATE_CALL,
                                                   "0xFE6BC(module[data[4]], G, data[4], data)"))
    print()
    print("    data block layout consumed here (payload+0xC):")
    print("      data[0]      = module type passed to the create helper (1..3 allocate)")
    print("      data[1]      = parent/vap module index (lookup gate; ctx[1] must read 0)")
    print("      data[4]      = user id -> module index that is CREATED (<= 0x12)")
    print("      data[8]      = flag copied to a global when type == 2")
    print("      data[0x11], data[0x12] = flags read by the create helper (0 = default)")
    print()
    print("[5] context-create helper CA 0x%06x:" % CPU_CREATE_HELPER)
    blob.print_dis(CPU_CREATE_HELPER, 0x40, mark=(CPU_CREATE_STORE,
                                                  "str.w r0,[ctx,#0x190]"))
    print("    data-block reads in this helper: data[0], data[0x11], data[0x12]")
    print("    the helper copies G[1],G[2] -> ctx[1],ctx[2] and G[4..7] -> ctx[8]")
    print("    and stores the 4th argument (data[4]) into ctx[0]")
    print("    module-type gate: type = data[0]; the +0x190 allocation is reached only for")
    print("    type 1/2/3; type 0/4/5/6 return 0 without allocating (CA 0xFF940 path)")
    print()
    callers = scan_callers(blob, CPU_CREATE_HELPER)
    print("[6] callers of CA 0x%06x: %s" % (CPU_CREATE_HELPER,
                                             ", ".join("CA 0x%06x" % c for c in callers)))
    print("    CA 0x%06x = boot task per-record body:" % BOOT_CREATE_CALL)
    print("      lookup index 0x%02x = boot-created module 18, data = zeroed 0x18-byte block"
          % BOOT_CREATE_ID)
    print("      -> data[0] = 0 -> type 0 -> NO +0x190 allocation on boot")
    print("    CA 0x%06x = create worker (our H2D route)" % WORKER_CREATE_CALL)
    print()
    print("[7] TXQ config sync (device capture: g0 id 1, hdr[3]=4, key 0x00d8) worker")
    print("    CA 0x%06x:" % CPU_WORKER_TXQ)
    blob.print_dis(CPU_WORKER_TXQ, 0x10, mark=(0x8103C, "r3 = [ctx+0x190]"))
    print("    -> on a boot with no created context r3 = 0 and CA 0x%06x stores through NULL:"
          % CPU_WORKER_TXQ_STORE)
    print("       the PD4 bench fault (data abort, then PC 0x81039/0x81040)")
    print()
    print("[8] why a create must precede the sync:")
    print("    boot fills all 19 module-table pointers (CA 0x49204 chain) with zeroed bodies,")
    print("    and the boot's own create call uses type 0 (no +0x190).  +0x190 has exactly")
    print("    two writers in the whole image: CA 0xFE7E4 (create helper) and CA 0x44ADE")
    print("    (clear on create failure).  The only host route to the helper is g0 id1,")
    print("    key 0x0064.  Therefore: g0 id1 key 0x64 (create) -> then g0 id1 key 0xd8 sync.")


def cmd_tables(blob):
    print("== key table A CA 0x%06x (%d entries) ==" % (CPU_TABLE_A, TAB_A_N))
    for k, a, key, w in table_decode(blob, CPU_TABLE_A, TAB_A_N):
        if key in (4, KEY_CREATE, 0x65, 0x66, 0x67, 0x69, KEY_TXQ_SYNC, 0x320):
            print("  A[%3d] @CA 0x%06x key=0x%04x worker=CA 0x%06x" % (k, a, key, w))
    print()
    print("== key table B CA 0x%06x (%d entries) ==" % (CPU_TABLE_B, TAB_B_N))
    for k, a, key, w in table_decode(blob, CPU_TABLE_B, TAB_B_N):
        if key in (4, KEY_CREATE, KEY_TXQ_SYNC, 0x320):
            print("  B[%3d] @CA 0x%06x key=0x%04x worker=CA 0x%06x" % (k, a, key, w))
    print("== key 0x%04x -> %s ==" % (KEY_TXQ_SYNC, key_worker(blob, KEY_TXQ_SYNC)))


def bar0(cpu):
    return cpu + BAR0_OFF


def cmd_readback(blob):
    print("== bench read-only pack (paused/booted chip; BAR0 = CPU + 0x%x) ==" % BAR0_OFF)
    print()
    rows = [
        ("CA 0x%06x" % G_SINGLETON, bar0(G_SINGLETON), 4,
         "0x00170E28 = G", "boot per-record body ran (worker G singleton)"),
        ("CA 0x%06x" % CPU_G_CTX4, bar0(CPU_G_CTX4), 4,
         "0x0011C458", "module-table entry 4 = *(G+(0x18C+4)*4) = 0x119758+4*0xB40"),
        ("CA 0x%06x" % CPU_G_COM, bar0(CPU_G_COM), 2,
         "0x0000", "u16 G+0x6DC common offset added by the lookup"),
        ("CA 0x%06x" % CPU_CTX4_190, bar0(CPU_CTX4_190), 4,
         "0 BEFORE / non-zero AFTER create", "ctx4+0x190 (field the sync needs)"),
        ("CA 0x%06x" % CPU_CTX4, bar0(CPU_CTX4), 4,
         "byte0 = user id (4), byte4 = type (1..3)", "ctx4 body: ctx[0]=data[4], ctx[4]=data[0]"),
        ("CA 0x%06x" % (CPU_G + G_REFCOUNT + 4), bar0(CPU_G + G_REFCOUNT + 4), 1,
         "0x01", "G+0x6C8+4 refcount byte, +1 by CA 0xBC726 in the create worker"),
        ("CA 0x400008", 0x400008, 4, "0x00000000", "exception vector mask (must stay 0)"),
        ("CA 0x40000C", 0x40000C, 0x40, "17-word first exception frame (PD4)",
         "unchanged if no exception occurs"),
        ("CA 0x108240", bar0(0x108240), 0x40, "PD4 stub bytes", "read-only cave integrity"),
    ]
    print("  %-12s %-10s %-5s %-40s %s" % ("CPU", "BAR0", "len", "expected", "meaning"))
    for cpu, b0, ln, exp, meaning in rows:
        print("  %-12s 0x%08X %-5d %-40s %s" % (cpu, b0, ln, exp, meaning))
    print()
    print("after create, with V = u32(BAR0 0x%08X) (the allocated buffer):" % bar0(CPU_CTX4_190))
    for off, exp in ((0x10, "0x00000064"), (0x14, "0x00000003"),
                     (0x238, "0x0000092a"), (0x52, "0x01")):
        print("  buffer+0x%03x  BAR0 = V + 0x6B8%03x  expect %s" % (off, off, exp))
    print("  (create helper writes +0x10=0x64, +0x14=3, +0x238=0x92a, +0x52=1;")
    print("   buffer+0x4c is later written by the TXQ sync worker with payload+0xC[0])")
    print()
    print("window note: firmware-RAM CPU addresses in the region-5 BAR0 window")
    print("(host BAR0 = CPU + 0x6B8000, size 0x218000); no device RAM write is needed.")


def cmd_selftest(blob):
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    ck("vendor firmware sha256", blob.sha256 == FW_SHA256, blob.sha256)
    ck("firmware length", len(blob.d) == FW_LEN, str(len(blob.d)))
    ck("handler prologue", blob.win(CPU_G0ID1_HANDLER, 4) == bytes.fromhex("70b538b9"),
       blob.win(CPU_G0ID1_HANDLER, 4).hex())
    ck("handler reads hdr[3]", blob.win(0x43D72, 2) == bytes.fromhex("c178"))
    ck("handler calls lookup(0,hdr[3])", blob.win(0x43D76, 4) == bytes.fromhex("baf0fffb"))
    ck("handler table-A literal", blob.u32(0x43E18) == CPU_TABLE_A)
    ck("handler table-B literal", blob.u32(0x43E1C) == CPU_TABLE_B)
    ck("group-0 table entry id 1", blob.win(CPU_G0_TABLE + 12, 2) == bytes([1, 0]),
       blob.win(CPU_G0_TABLE + 12, 2).hex())
    ck("group-0 table id 1 -> CA 0x43D59",
       blob.u32(CPU_G0_TABLE + 12 + 4) == CPU_G0ID1_HANDLER + 1,
       hex(blob.u32(CPU_G0_TABLE + 12 + 4)))
    ck("lookup tail-call site", blob.win(CPU_LOOKUP_TAILCALL, 8) ==
       bytes.fromhex("0146002068f0e1b9"), blob.win(CPU_LOOKUP_TAILCALL, 8).hex())
    kw = key_worker(blob, KEY_CREATE)
    ck("key 0x64 in table A entry 1", kw is not None and kw[0] == "A" and kw[1] == 1 and
       kw[3] == CPU_WORKER_CREATE + 1, repr(kw))
    kx = key_worker(blob, KEY_TXQ_SYNC)
    ck("key 0xd8 in table A entry 41 -> worker 0x81039",
       kx is not None and kx[0] == "A" and kx[1] == 41 and kx[3] == 0x81039, repr(kx))
    ck("worker-create prologue", blob.win(CPU_WORKER_CREATE, 4) == bytes.fromhex("2de9f04f"))
    ck("worker-create reads data[1]",
       blob.win(CPU_WORKER_CREATE + 8, 4) == bytes.fromhex("51780020"))
    ck("worker-create lookup for data[4]", blob.win(0x44A4E, 4) ==
       bytes.fromhex("51f0aefb"), blob.win(0x44A4E, 4).hex())
    ck("worker-create calls the helper", blob.win(WORKER_CREATE_CALL, 4) ==
       bytes.fromhex("b9f006fe"), blob.win(WORKER_CREATE_CALL, 4).hex())
    ck("helper prologue", blob.win(CPU_CREATE_HELPER, 4) == bytes.fromhex("2de9f047"))
    ck("helper stores ctx+0x190", blob.win(CPU_CREATE_STORE, 4) == bytes.fromhex("c4f89001"))
    ck("helper data[0] read", blob.win(0xFE6EC, 4) == bytes.fromhex("31780171"))
    ck("helper data[0x11] read", blob.win(0xFE6F0, 2) == bytes.fromhex("727c"))
    ck("helper data[0x12] read", blob.win(0xFE6F4, 2) == bytes.fromhex("b27c"))
    ck("helper alloc call", blob.win(0xFE7E0, 4) == bytes.fromhex("c0f7a2fc"))
    ck("helper alloc-fail return 0x67", blob.win(0xFE800, 2) == bytes.fromhex("6720"))
    ck("helper type gate (type-1 compare vs 2)", blob.win(0xFE7CC, 4) ==
       bytes.fromhex("013b022b"), blob.win(0xFE7CC, 4).hex())
    ck("helper type>3 no-alloc path CA 0xFF940", blob.win(0xFF940, 2) ==
       bytes.fromhex("2b23"), blob.win(0xFF940, 2).hex())
    ck("boot create call site", blob.win(BOOT_CREATE_CALL, 4) ==
       bytes.fromhex("b5f02bff"), blob.win(BOOT_CREATE_CALL, 4).hex())
    ck("boot data memset (sp+0x54, 0x18)", blob.win(CPU_BOOT_DATA_MEMSET, 8) ==
       bytes.fromhex("1822214615a8baf0"), blob.win(CPU_BOOT_DATA_MEMSET, 8).hex())
    ck("boot create id 0x12", blob.win(0x487F4, 4) == bytes.fromhex("122073f0"))
    callers = scan_callers(blob, CPU_CREATE_HELPER)
    ck("helper has exactly 2 callers", len(callers) == 2, repr([hex(c) for c in callers]))
    ck("helper callers = boot task + create worker",
       sorted(callers) == sorted([BOOT_CREATE_CALL, WORKER_CREATE_CALL]),
       repr([hex(c) for c in callers]))
    ck("module 4 block = 0x11C458", CPU_CTX4 == 0x11C458, hex(CPU_CTX4))
    ck("G table entry for id 4 = 0x171468", CPU_G_CTX4 == 0x171468, hex(CPU_G_CTX4))
    ck("ctx4+0x190 = 0x11C5E8", CPU_CTX4_190 == 0x11C5E8, hex(CPU_CTX4_190))
    ck("BAR0 ctx4+0x190", bar0(CPU_CTX4_190) == 0x7D45E8, hex(bar0(CPU_CTX4_190)))
    ck("BAR0 G table entry 4", bar0(CPU_G_CTX4) == 0x829468, hex(bar0(CPU_G_CTX4)))
    ck("sync worker prologue", blob.win(CPU_WORKER_TXQ, 4) == bytes.fromhex("08b51178"),
       blob.win(CPU_WORKER_TXQ, 4).hex())
    ck("sync worker loads ctx+0x190", blob.win(0x8103C, 4) == bytes.fromhex("d0f89031"))
    ck("sync worker stores through it", blob.win(CPU_WORKER_TXQ_STORE, 4) ==
       bytes.fromhex("83f84c10"))
    ck("user-manager init memsets G", blob.win(CPU_USER_MGR_INIT, 8) ==
       bytes.fromhex("4ff4e063414c0246"), blob.win(CPU_USER_MGR_INIT, 8).hex())
    ck("refcount helper returns 0 for id<=0x12", blob.win(0xBC726, 4) ==
       bytes.fromhex("12289fbf"), blob.win(0xBC726, 4).hex())

    bad = 0
    for name, ok, detail in checks:
        print("%s  %s%s" % ("PASS" if ok else "FAIL", name,
                            ("  [%s]" % (detail,)) if not ok else ""))
        bad += 0 if ok else 1
    print("\n%d/%d PASS" % (len(checks) - bad, len(checks)))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fw", default=DEFAULT_FW)
    ap.add_argument("--trace", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--readback", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    blob = Blob(args.fw)
    if args.selftest:
        return cmd_selftest(blob)
    if args.readback:
        cmd_readback(blob)
        return 0
    if args.tables:
        cmd_tables(blob)
        return 0
    cmd_trace(blob)
    return 0


if __name__ == "__main__":
    sys.exit(main())
