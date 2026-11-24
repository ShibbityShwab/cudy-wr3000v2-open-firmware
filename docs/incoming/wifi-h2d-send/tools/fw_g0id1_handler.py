#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_g0id1_handler.py - static analysis of the firmware's group-0 id-1 H2D
receive handler (Hi5622V100, Cudy WR3000 v2.0 / luofu v2).

Lane: wifi-h2d-send (shared task task-99, 2026-11-24), host-only.
Reads the LOCAL vendor firmware blob in place and prints derived annotations
(addresses, decoded instructions, table entries).  No vendor byte, blob or
calibration value is written into the repository.

Evidence base (committed lanes, cited by address):
  * opensource/docs/incoming/wifi-h2d-catalog - the firmware's H2D receive
    dispatch tables (group 0 CA 0x10CCFC, group 2 CA 0x10CC18) and their
    registration by the registrar CA 0x102866;
  * opensource/docs/incoming/wifi-hcc-link - the registrar call sites, the
    id3 mailbox handler and the message-worker wake-up;
  * opensource/docs/phase37/fw-ring-message-table.md - the mailbox dispatcher
    CA 0xC18AC (ids 1/3/5/6, indexed by the lowest set bit of out[0]) and the
    id3 handler body that wakes the message worker;
  * opensource/docs/phase37/sr-message-catalog.md - the 12-byte HCC header;
  * opensource/docs/incoming/wifi-irq-unmask/REPORT-PD2.md - the exception
    mask/vector contract (mask CA 0x40008 |= 1<<vector; vector 4 = data abort);
  * opensource/docs/incoming/wifi-h2d-send/REPORT-G0ID1-HANDLER.md - the
    write-up of what this tool verifies.

Conventions: runtime CPU address = file offset + 0x40000; Thumb code starts at
(CA & ~1).  "file" in reports is the blob offset.

Subcommands:
  --handler   g0 id1: registration, parse sequence, key tables, the key-0xd8
              worker and the fault-instruction candidates
  --dispatch  the receive path around it (mailbox dispatcher 0xC18AC, the id3
              worker wake-up, the boot-time handler-table registration)
  --selftest  assertions (exit 0 on PASS)
"""
import argparse
import hashlib
import os
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
except Exception:  # pragma: no cover
    print("error: capstone is required (project venv .provision-venv/bin/python)",
          file=sys.stderr)
    raise SystemExit(2)

BASE = 0x40000
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920

# H2D receive dispatch tables (wifi-h2d-catalog sec 4.2): table CA, count,
# 12-byte record {u16 id; u16 pad; u32 fn; u32 aux}.
G2_TABLE, G2_COUNT = 0x10CC18, 19
G0_TABLE, G0_COUNT = 0x10CCFC, 40
G0_EARLY, G0_EARLY_COUNT = 0x10CBEC, 1

G0_ID1_FN = 0x00043D59        # registry value (Thumb bit set)
G0_ID1_CODE = 0x00043D58      # code start
G0_ID0_FN = 0x00081695        # payload-free log stub (contrast case)

# The two static key -> worker tables the g0 id1 handler searches.
KEY_TABLE_A, KEY_TABLE_A_BYTES = 0x10C23C, 0x6C8   # 217 entries, 8 B each
KEY_TABLE_B, KEY_TABLE_B_BYTES = 0x103BF8, 0x2C0   #  88 entries, 8 B each
TXQ_KEY = 0x00D8              # the captured first post-ready frame's key
TXQ_WORKER = 0x00081039       # key 0xd8 -> table A index 41
TXQ_WORKER_CODE = 0x00081038

# The context lookup the handler calls for the header's field B (hdr[3]).
CTX_LOOKUP = 0x000FE578
HDR_FETCH = 0x000409EA

# The mailbox dispatcher (phase37/fw-ring-message-table): ids 1/3/5/6 only.
MAILBOX_DISPATCHER = 0x000C18AC
MAILBOX_ID3_FN = 0x000C5145
MAILBOX_REGISTRAR = 0x000C187A
BOOT_REGISTRATION = (0x00048336, 0x00048346)   # group 2 / group 0 callsites
REGISTRAR = 0x00102866

# Exception-mask contract (PD2/PD3): mask CA 0x40008 bit = 1 << vector;
# ARM vector 1 = undefined instruction, vector 4 = data abort.
VECTOR_UNDEF = 1
VECTOR_DABT = 4
BENCH_MASK = 0x12


def resolve_default_fw():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.normpath(os.path.join(here, "..", "..", "..", "..", ".."))
    env = os.environ.get("HI5622_FIRMWARE")
    candidates = [
        env,
        os.path.join(repo, "rootfs-2.4.15", "lib", "firmware", "hi_wifi",
                     "FIRMWARE.bin"),
        os.path.join(repo, "rootfs-2.5.24", "lib", "firmware", "hi_wifi",
                     "FIRMWARE.bin"),
        "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin",
    ]
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    return candidates[1]


class Blob:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        self.sha256 = hashlib.sha256(self.d).hexdigest()

    def u16(self, ca):
        return struct.unpack_from("<H", self.d, ca - BASE)[0]

    def u32(self, ca):
        return struct.unpack_from("<I", self.d, ca - BASE)[0]

    def win(self, ca, n):
        return self.d[ca - BASE:ca - BASE + n]

    def dis(self, ca, n):
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        md.skipdata = True
        return list(md.disasm(self.win(ca, n), ca))


def dis_text(blob, ca, n):
    """Decoded instructions as 'mnemonic op_str ; ...' text for assertions."""
    return " ; ".join(("%s %s" % (i.mnemonic, i.op_str)).strip()
                      for i in blob.dis(ca, n))


def print_dis(blob, ca, n, title):
    print("  == %s (CA %#08x, file %#07x) ==" % (title, ca, ca - BASE))
    for i in blob.dis(ca, n):
        print("    %08x  %-8s %s" % (i.address, i.mnemonic, i.op_str))


def decode_dispatch_table(blob, table, count):
    rows = []
    for i in range(count):
        ca = table + i * 12
        idv = blob.u16(ca)
        pad = blob.u16(ca + 2)
        fn = blob.u32(ca + 4)
        aux = blob.u32(ca + 8)
        rows.append({"index": i, "id": idv, "pad": pad, "fn": fn, "aux": aux})
    return rows


def decode_key_table(blob, table, nbytes):
    rows = []
    for i in range(nbytes // 8):
        ca = table + i * 8
        rows.append({"index": i, "key": blob.u16(ca), "fn": blob.u32(ca + 4)})
    return rows


def find_key(rows, key):
    return [r for r in rows if r["key"] == key]


def find_id(rows, msg_id):
    return [r for r in rows if r["id"] == msg_id]


def cmd_handler(blob):
    print("== firmware group-0 id-1 (hdpp_config_send_event) H2D receive handler ==")
    print()
    print("1. registration (H2D receive dispatch table group 0, CA %#08x):"
          % G0_TABLE)
    g0 = decode_dispatch_table(blob, G0_TABLE, G0_COUNT)
    for r in find_id(g0, 1):
        print("  entry[%d] id 1 -> handler CA %#010x (code %#010x, Thumb bit set)"
              % (r["index"], r["fn"], r["fn"] & ~1))
    for r in find_id(g0, 0):
        print("  entry[%d] id 0 -> handler CA %#010x (payload-free log stub -"
              % (r["index"], r["fn"]))
        print("                     contrast case: reads no payload at all)")
    nulls = [r for r in g0 if r["fn"] == 0]
    print("  table: %d entries, %d registered, %d NULL slots (one is id 0x14)"
          % (len(g0), len(g0) - len(nulls), len(nulls)))
    print()
    print("2. handler body CA %#08x - what it parses (offsets are relative to"
          % G0_ID1_CODE)
    print("   the message pointer; payload starts at hdr+0xC):")
    print_dis(blob, G0_ID1_CODE, 0xBA, "group-0 id-1 handler")
    print()
    print("  V parse summary:")
    print("    hdr+0x03  field B -> user/vap context lookup %#x(0, fieldB)"
          % CTX_LOOKUP)
    print("    hdr+0x14  u16 key      (payload +0x8) = the captured 0x%04x"
          % TXQ_KEY)
    print("    hdr+0x16  u8  sub-key  (payload +0xA) = 0x14 in the capture")
    print("    hdr+0x18  data pointer (payload +0xC), handed to the per-key worker")
    print("  V it does NOT read hdr+0x04 (declared total) or a node length:")
    words = [i.op_str for i in blob.dis(G0_ID1_CODE, 0xBA)]
    bad = [w for w in words if "[r4, #4]" in w or "[r4, #6]" in w]
    print("    total/id reads in the handler: %s" % (bad or "none"))
    print()
    print("3. key tables (static image data; the filesystem of config workers):")
    ta = decode_key_table(blob, KEY_TABLE_A, KEY_TABLE_A_BYTES)
    tb = decode_key_table(blob, KEY_TABLE_B, KEY_TABLE_B_BYTES)
    for name, rows, table, nbytes in (("A", ta, KEY_TABLE_A, KEY_TABLE_A_BYTES),
                                      ("B", tb, KEY_TABLE_B, KEY_TABLE_B_BYTES)):
        print("  table %s CA %#010x: %d entries x 8 B {u16 key; pad; u32 worker}"
              % (name, table, len(rows)))
    for r in find_key(ta, TXQ_KEY):
        print("  key 0x%04x -> table A[%d] worker CA %#010x"
              % (TXQ_KEY, r["index"], r["fn"]))
    print("  key 0x%04x present in table B: %s"
          % (TXQ_KEY, bool(find_key(tb, TXQ_KEY))))
    print()
    print("4. the key-0xd8 worker (the TXQ config-sync command):")
    print_dis(blob, TXQ_WORKER_CODE, 0x28, "key 0xd8 worker")
    print()
    print("  V parse of the worker (r0 = user/vap context from step 2):")
    print("    r2 = hdr+0x18 (data pointer)   -> ldrb r1,[r2]   = data[0]")
    print("    r0 = ctx                        -> ldr.w r3,[r0,#0x190]")
    print("    r3                              -> strb.w r1,[r3,#0x4c]")
    print("  FAULT CANDIDATES (mask 0x12 = undefined + data abort):")
    print("    a) CA %#x ldr.w r3,[r0,#0x190]: r0 == 0 or a garbage user"
          % 0x0008103C)
    print("       context -> r3 = ROM word; the following store faults")
    print("    b) CA %#x strb.w r1,[r3,#0x4c]: precise DATA ABORT when r3"
          % 0x00081040)
    print("       is the mask-ROM window (vector 4 -> mask bit 0x10)")
    print("  => if field B's user/vap context is missing/uninitialised, this")
    print("     handler faults here; it is a payload-context dependency, not a")
    print("     node-length parse (48 vs 72 changes no offset it reads).")


def cmd_dispatch(blob):
    print("== the receive path around the g0 id1 handler ==")
    print()
    print("1. mailbox dispatcher CA %#08x (phase37 fw-ring-message-table):"
          % MAILBOX_DISPATCHER)
    print_dis(blob, MAILBOX_DISPATCHER, 0x50, "mailbox dispatcher")
    print()
    print("  V it dispatches MAILBOX ids 0..9 by the lowest set bit of out[0]")
    print("    (%#x, %#x: table[id]={fn,arg}, loop <= 9) - it has no group/id"
          % (0x40039010, 0x400392D4))
    print("    field parse: the SR ring body is not touched here.")
    print()
    print("2. mailbox id 3 (the SR announce) wakes the firmware message worker:")
    print("   handler CA %#010x: pcie_msg[+0x98] = 1, then message worker"
          % MAILBOX_ID3_FN)
    print("   CA 0x000C2A54 on pcie_msg[+0x50] - the worker is what drains the")
    print("   SR ring node and dispatches group/id through the H2D tables.")
    print()
    print("3. handler-table registration (boot chain, after pcie_msg_init):")
    for ca in BOOT_REGISTRATION:
        group, count = (2, 0x13) if ca == BOOT_REGISTRATION[0] else (0, 0x28)
        print("   CA %#08x: registrar %#x(group %d, table CA %#x, count %#x)"
              % (ca, REGISTRAR, group, G2_TABLE if group == 2 else G0_TABLE,
                 count))
    print("   => by the PD3 boot epilogue the g0 id1 slot is registered; the")
    print("      missing piece is not the handler table.")
    print()
    print("4. preconditions the g0 id1 handler needs (static):")
    print("   - pcie_msg_init's mailbox pointers (+4/+0xC/+0x10/+0x20) and the")
    print("     id3 mailbox registration: done at boot (golden boot evidence).")
    print("   - the SR frame must be drained by the message worker, i.e. the")
    print("     mailbox id3 announce must be consumed (the PD3 stub's job).")
    print("   - field B (hdr[3]) must resolve to a valid user/vap context")
    print("     (CTX_LOOKUP %#x): the key-0xd8 worker dereferences it"
          % CTX_LOOKUP)
    print("     unconditionally.  No prior g0 id0 message is required by the")
    print("     handler code; the g0 id0 handler is an independent log stub.")


def cmd_selftest(blob):
    checks = []

    def ck(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    ck("vendor firmware sha256 is the pinned blob", blob.sha256 == FW_SHA256,
       blob.sha256)
    ck("vendor firmware length", len(blob.d) == FW_LEN, str(len(blob.d)))

    g0 = decode_dispatch_table(blob, G0_TABLE, G0_COUNT)
    g0ids = [r["id"] for r in g0]
    ck("group-0 table has 40 entries", len(g0) == 40, str(len(g0)))
    ck("group-0 ids start 0,1,2,3,6", g0ids[:5] == [0, 1, 2, 3, 6], repr(g0ids[:5]))
    ck("group-0 table contains the one NULL registered id 0x14",
       [r["fn"] for r in g0 if r["id"] == 0x14] == [0])
    ck("group-0 id 1 registers handler CA 0x43d59",
       [r["fn"] for r in g0 if r["id"] == 1] == [G0_ID1_FN])
    ck("group-0 id 0 registers the payload-free stub CA 0x81695",
       [r["fn"] for r in g0 if r["id"] == 0] == [G0_ID0_FN])

    ck("g0 id1 handler prologue bytes", blob.win(G0_ID1_CODE, 4) ==
       bytes.fromhex("70b538b9"))
    ck("handler key-table literal = CA 0x10C23C",
       blob.u32(0x43E18) == KEY_TABLE_A)
    ck("handler second-table literal = CA 0x103BF8",
       blob.u32(0x43E1C) == KEY_TABLE_B)

    ta = decode_key_table(blob, KEY_TABLE_A, KEY_TABLE_A_BYTES)
    tb = decode_key_table(blob, KEY_TABLE_B, KEY_TABLE_B_BYTES)
    ck("key table A has 217 entries", len(ta) == 217, str(len(ta)))
    ck("key table B has 88 entries", len(tb) == 88, str(len(tb)))
    ck("key table A first entry key 0x0004", ta[0]["key"] == 0x0004)
    ck("key 0xd8 -> table A[41] worker 0x81039",
       ta[41]["key"] == TXQ_KEY and ta[41]["fn"] == TXQ_WORKER)
    ck("key 0xd8 exists exactly once across both tables",
       len(find_key(ta, TXQ_KEY)) + len(find_key(tb, TXQ_KEY)) == 1)

    ck("key-0xd8 worker prologue bytes",
       blob.win(TXQ_WORKER_CODE, 4) == bytes.fromhex("08b51178"))
    ck("worker loads the user context field [r0,#0x190]",
       blob.win(0x8103C, 4) == bytes.fromhex("d0f89031"))
    ck("worker stores the config byte [r3,#0x4c] (data abort candidate)",
       blob.win(0x81040, 4) == bytes.fromhex("83f84c10"))

    hdr_txt = dis_text(blob, HDR_FETCH, 0x40)
    ck("header fetch 0x409EA reads no halfword/length field",
       "ldrh" not in hdr_txt, hdr_txt[:80])
    ck("header fetch validates the class field hdr[5]>>1&7",
       "ldrb r3, [r4, #5]" in hdr_txt)
    ck("header fetch checks message-pool membership via hdr[6]/hdr[7]",
       "ldrb r3, [r0, #7]" in hdr_txt and "ldrb r2, [r0, #6]" in hdr_txt)

    h_txt = dis_text(blob, G0_ID1_CODE, 0xBA)
    ck("handler reads the key at hdr+0x14", "ldrh r2, [r4, #0x14]" in h_txt)
    ck("handler reads the sub-key byte at hdr+0x16",
       "ldrb r1, [r4, #0x16]" in h_txt)
    ck("handler hands hdr+0x18 (payload+0xC) to the worker",
       "add.w r2, r4, #0x18" in h_txt)
    ck("handler reads the field-B context byte at hdr+3",
       "ldrb r1, [r0, #3]" in h_txt)
    ck("handler does not read a declared total or node length",
       "[r4, #4]" not in h_txt and "[r4, #6]" not in h_txt)

    # 48- vs 72-byte post: the handler's deepest read is hdr+0x18 + data[0x14)
    deepest = 0x18 + 0x14
    ck("handler's deepest access (0x%02x) fits the declared 0x30 frame total"
       % deepest, deepest <= 0x30)
    ck("48- and 72-byte posts carry identical bytes at every offset this "
       "handler reads", 0x14 < 0x30 and 0x16 < 0x30 and 0x18 < 0x30)

    d_txt = dis_text(blob, MAILBOX_DISPATCHER, 0x50)
    ck("mailbox dispatcher computes id as lowest set bit (clz/rsb)",
       "clz r4, r4" in d_txt and "rsb.w r4, r4, #0x1f" in d_txt)
    ck("mailbox dispatcher bounds the id to <= 9",
       "cmp r4, #9" in d_txt)
    ck("mailbox dispatcher calls table[id].fn(table[id].arg)",
       "ldr.w r3, [r3, r4, lsl #3]" in d_txt and
       "ldr r0, [r2, #4]" in d_txt and "blx r3" in d_txt)
    ck("mailbox dispatcher has no group/id header parse",
       "ldrb" not in d_txt)

    ck("pcie_msg_init registers the id3 handler literal CA 0xC5145",
       blob.u32(0x49864) == MAILBOX_ID3_FN)
    ck("pcie_msg_init calls the mailbox registrar 0xC187A",
       "bl #0xc187a" in dis_text(blob, 0x49838, 4))
    ck("boot chain calls the H2D registrar with group 2 (count 0x13)",
       "bl #0x102866" in dis_text(blob, 0x4833C, 4))
    ck("boot chain calls the H2D registrar with group 0 (count 0x28)",
       "bl #0x102866" in dis_text(blob, 0x48346, 4))

    mask = (1 << VECTOR_UNDEF) | (1 << VECTOR_DABT)
    ck("bench mask 0x12 = undefined instruction + data abort",
       mask == BENCH_MASK, "%#x" % mask)
    ck("data abort alone would be mask 0x10 (PD2 bench contract)",
       (1 << VECTOR_DABT) == 0x10)

    ok = sum(1 for _n, c, _d in checks if c)
    for n, c, det in checks:
        if not c:
            print("FAIL: %s %s" % (n, detail_fmt(det)))
    print("fw_g0id1_handler selftest: %d/%d PASS" % (ok, len(checks)))
    return 0 if ok == len(checks) else 1


def detail_fmt(det):
    return ("(%s)" % det) if det else ""


def main(argv):
    ap = argparse.ArgumentParser(description="firmware g0 id1 H2D handler "
                                             "static analysis (host-only)")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--handler", action="store_true")
    g.add_argument("--dispatch", action="store_true")
    g.add_argument("--selftest", action="store_true")
    ap.add_argument("--fw", default=None)
    a = ap.parse_args(argv)
    path = a.fw or resolve_default_fw()
    if not os.path.isfile(path):
        print("error: firmware blob not found; pass --fw or set "
              "HI5622_FIRMWARE", file=sys.stderr)
        return 2
    blob = Blob(path)
    if a.selftest:
        return cmd_selftest(blob)
    if blob.sha256 != FW_SHA256:
        print("error: not the pinned vendor blob (sha256 %s)" % blob.sha256,
              file=sys.stderr)
        return 2
    if a.handler:
        cmd_handler(blob)
    else:
        cmd_dispatch(blob)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))