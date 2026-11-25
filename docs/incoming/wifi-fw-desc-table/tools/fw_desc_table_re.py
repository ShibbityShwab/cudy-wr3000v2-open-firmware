#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_desc_table_re.py - re-derive the H2D 0x00F7 descriptor-table decode.

Lane wifi-fw-desc-table (task-187). HOST-ONLY: reads the pinned local vendor
modules and firmware image in place; no device contact, no writes.

Every claim of REPORT.md/TABLES.md is asserted here against the artifacts:

  --selftest   assert the decode (emitter, builder, blob filler, gate, boot
               pool, worker, predicate, table A row) + the exact fix frame
  --frame      emit the byte-exact 2-record 0x00F7 frame
  --tables     print the frame/blob/probe tables
  --probes     print the read-only probe pack (CPU/BAR0/phys)

Artifacts are read-only and never copied into the repository; only derived
addresses/constants are printed. Exit 0 = all assertions pass.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

FW_DEFAULT = ("/home/shibbityshwab/recovery-extract/lib/firmware/"
              "hi_wifi/FIRMWARE.bin")
KO_DIR = "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/"
WIFI_KO = "hi5622v100_wifi.ko"
PLAT_KO = "hi5622v100_plat.ko"
OD_DEFAULT = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-objdump")
NM_DEFAULT = ("/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/"
              "toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/"
              "arm-openwrt-linux-nm")
OD_LOCAL = ("build/linux-kbuild/toolchain/bin/"
            "arm-none-linux-gnueabihf-objdump")
NM_LOCAL = ("build/linux-kbuild/toolchain/bin/"
            "arm-none-linux-gnueabihf-nm")

FW_IMAGE_END = 0x122C98          # image bytes end at this CA
POOL = 0x1ADC98                  # runtime descriptor pool
MAP_B0 = 0x117B48                # 14 rows
MAP_B1 = 0x117AD4                # 29 rows
TABLE_A = 0x10C23C               # 217 x 8-byte {u16 key; u16 pad; u32 worker}
ROW_0F7 = 92                     # table-A row for key 0x00F7
WORKER_0F7 = 0x45A30
GATE = 0x1037BC
POPULATOR = 0x1017F8
MEMCPY_S = 0xC1E44
PREDICATE = 0x101890
LATCH = 0x89840
HANDLER_G0ID1 = 0x43D58
BOOT_CC_LITERAL = 0x1043A2
BOOT_CC = b"99\x00"
CC_FIX = b"US"
BW_20MHZ = 0x02
BAND0_BITMAP = 0x00001FFF
BAND1_BITMAP_CH36_64 = 0x000000FF
BAND1_BITMAP_ALL = 0x1FFFFFFF
# vendor module .text offsets
EMITTER = 0x63B6C
EMIT_SITE = 0x63C3C
BUILDER = 0x5717C
HOST_TWIN = 0x206E0
HOST_TWIN_MAP = 0x205E4
WAL_THUNK = 0x12AE0C
FILLER = 0x121E10
WAL_UPDATE = 0x1226F0
WAL_UPDATE_SITE = 0x12290C
WAL_GET_SITE = 0x108454
INI_COUNTRY = 0x1558
INI_ENTRY = 0x122FE0
HIPRIV_SETCOUNTRY = 0x122D0C
DESC_MAX = 0x148
N_MAX = (DESC_MAX - 8) // 16        # 20

CHECKS = []
FAILS = []


def check(ok, label, detail=""):
    CHECKS.append(label)
    if ok:
        print("PASS  " + label)
    else:
        FAILS.append(label + ("  [" + detail + "]" if detail else ""))
        print("FAIL  " + label + ("  [" + detail + "]" if detail else ""))


def find_tool(env, default, local):
    p = os.environ.get(env) or default
    if os.path.isfile(p) and os.access(p, os.X_OK):
        return p
    for cand in (os.path.join(os.getcwd(), local),
                 shutil.which(os.path.basename(local))):
        if cand and os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    return None


def norm(text):
    out = []
    for line in text.splitlines():
        s = line.rstrip()
        if not s.strip():
            continue
        out.append(" ".join(s.split()))
    return "\n".join(out)


def run(cmd):
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=600)
    except OSError as exc:
        sys.exit("error: cannot run %r: %s" % (cmd, exc))
    return r.stdout.decode("utf-8", "replace")


def disasm_ko(od, ko, start, stop):
    txt = run([od, "-dr", "--no-show-raw-insn",
               "--start-address=0x%x" % start,
               "--stop-address=0x%x" % stop, ko])
    insns = {}
    relocs = {}
    for line in txt.splitlines():
        s = line.strip()
        if not s:
            continue
        if "R_ARM_" in s and ": R_ARM_" in s:
            toks = s.split()
            try:
                site = int(toks[0].rstrip(":"), 16)
            except ValueError:
                continue
            relocs.setdefault(site, []).append((toks[1], toks[2] if len(toks) > 2 else ""))
            continue
        cols = s.split(":\t")
        if len(cols) < 2:
            continue
        try:
            addr = int(cols[0], 16)
        except ValueError:
            continue
        body = cols[1].split("\t;")[0].split("\t@")[0]
        insns[addr] = " ".join(body.split())
    return insns, relocs


def disasm_bin(od, fw, start, stop, thumb=True, vma=0x40000):
    cmd = [od, "-D", "-b", "binary", "--no-show-raw-insn",
           "-m", "arm"]
    if thumb:
        cmd += ["-M", "force-thumb"]
    cmd += ["--adjust-vma=0x%x" % vma,
            "--start-address=0x%x" % start,
            "--stop-address=0x%x" % stop, fw]
    txt = run(cmd)
    insns = {}
    for line in txt.splitlines():
        s = line.strip()
        cols = s.split(":\t")
        if len(cols) < 2:
            continue
        try:
            addr = int(cols[0], 16)
        except ValueError:
            continue
        body = cols[1].split("\t;")[0].split("\t@")[0]
        toks = body.split()
        # flat-binary objdump keeps the raw encoding as the first token
        if toks and len(toks) > 1 and all(c in "0123456789abcdef" for c in toks[0]):
            toks = toks[1:]
        insns[addr] = " ".join(toks)
    return insns


def u8(img, ca):
    return img[ca - 0x40000]


def u16(img, ca):
    return struct.unpack_from("<H", img, ca - 0x40000)[0]


def u32(img, ca):
    return struct.unpack_from("<I", img, ca - 0x40000)[0]


def insn_at(insns, addr):
    return insns.get(addr, "<missing>")


# ---------------------------------------------------------------------------
# the fix message
# ---------------------------------------------------------------------------
def record(word, p4, p6, bitmap):
    b = struct.pack("<I", word) + bytes([p4, 0]) + struct.pack("<H", p6) + b"\x00" * 4 + struct.pack("<I", bitmap)
    assert len(b) == 16
    return b


def fix_blob(n=2, cc=CC_FIX, band1_bitmap=BAND1_BITMAP_CH36_64,
             band1_bw=BW_20MHZ, arm_2g=True, arm_5g=True):
    if n != 2:
        raise ValueError("this recipe ships exactly 2 records")
    if cc in (b"", b"99"):
        raise ValueError("cc must differ from the boot pool string '99'")
    if band1_bitmap & ~BAND1_BITMAP_ALL:
        raise ValueError("5G bitmap wider than the 29-row map")
    if band1_bw not in (2, 4, 5, 6):
        raise ValueError("bandwidth code must be 2/4/5/6")
    word0 = 0x00800000 if arm_2g else 0x00000000
    word1 = (band1_bw << 8) | (0x80 << 16) | 0x01 if arm_5g else (band1_bw << 8) | 0x01
    blob = cc + b"\x00\x00" + bytes([n, 0, 0, 0])
    blob += record(word0, 30, 300, BAND0_BITMAP)
    blob += record(word1, 30, 300, band1_bitmap)
    assert len(blob) == 8 + 16 * n
    return blob


def fix_frame(**kw):
    blob = fix_blob(**kw)
    payload = (b"\x00\x00\x00\x00\x01\x00\x00\x00"
               + struct.pack("<H", 0x00F7)
               + struct.pack("<H", len(blob))
               + blob + b"\xff" * 4)
    total = 12 + len(payload)
    frame = (bytes([0x00, 0x01, 0x00, 0x04])
             + struct.pack("<H", total)
             + struct.pack("<H", 0x0001)
             + bytes([0x00, 0x00, 0x5A, 0x5A])
             + payload)
    return frame, blob, total


FRAME_HEX = ("000100044400010000005a5a0000000001000000f7002800"
             "5553000002000000000080001e002c0100000000ff1f0000"
             "010280001e002c0100000000ff000000ffffffff")


# ---------------------------------------------------------------------------
# selftest
# ---------------------------------------------------------------------------
def selftest(od, nm, fw, ko_dir):
    wifi = os.path.join(ko_dir, WIFI_KO)
    plat = os.path.join(ko_dir, PLAT_KO)
    for p in (fw, wifi, plat):
        if not os.path.isfile(p):
            sys.exit("error: missing artifact " + p)
    with open(fw, "rb") as f:
        img = f.read()
    check(len(img) == 928920, "firmware size 928,920 B")
    check(len(img) + 0x40000 == FW_IMAGE_END,
          "image ends CA 0x%X" % FW_IMAGE_END,
          hex(len(img) + 0x40000))

    # 1) symbols
    syms = {}
    txt = run([nm, "-n", "--defined-only", wifi])
    for line in txt.splitlines():
        p = line.split()
        if len(p) >= 3:
            try:
                syms[p[2]] = int(p[0], 16)
            except ValueError:
                pass
    check(syms.get("hmac_config_set_country") == EMITTER,
          "hmac_config_set_country at .text+0x%X" % EMITTER,
          hex(syms.get("hmac_config_set_country", 0)))
    check(syms.get("hmac_config_send_event") == BUILDER,
          "hmac_config_send_event at .text+0x%X" % BUILDER,
          hex(syms.get("hmac_config_send_event", 0)))
    for name, addr in (("mac_regdomain_set_country", HOST_TWIN),
                       ("mac_init_channel_list", HOST_TWIN_MAP),
                       ("wal_config_set_country", WAL_THUNK),
                       ("wal_regdomain_update", WAL_UPDATE),
                       ("hwifi_config_init_ini_country", INI_COUNTRY),
                       ("hwifi_config_init_ini", INI_ENTRY),
                       ("wal_hipriv_setcountry", HIPRIV_SETCOUNTRY)):
        check(syms.get(name) == addr, name + " at .text+0x%X" % addr,
              hex(syms.get(name, 0)))

    # 2) the emitter call site: mov r1,#247 ; hmac_config_send_event
    ins, rel = disasm_ko(od, wifi, EMITTER, EMITTER + 0x100)
    check(insn_at(ins, 0x63C24) == "ldrb r1, [r6, #4]",
          "emitter: count = blob[4]", insn_at(ins, 0x63C24))
    check(insn_at(ins, 0x63C34) == "add r2, r2, r1, lsl #4",
          "emitter: len = 8 + count*16", insn_at(ins, 0x63C34))
    check(insn_at(ins, 0x63C38) == "mov r1, #247",
          "emitter: key = 0x00F7", insn_at(ins, 0x63C38))
    check(any(t == "R_ARM_CALL" and s == "hmac_config_send_event"
              for t, s in rel.get(EMIT_SITE, [])),
          "emitter: call site 0x%X -> hmac_config_send_event" % EMIT_SITE)
    ins, rel = disasm_ko(od, wifi, BUILDER, BUILDER + 0x120)
    check(any(s == "hcc_msg_tx" for t, s in rel.get(0x57210, [])),
          "builder: hcc_msg_tx at .text+0x57210")
    check(any(s == "memcpy_s" for t, s in rel.get(0x57200, [])),
          "builder: memcpy_s at .text+0x57200")
    check(insn_at(ins, 0x571F4) == "add r0, r0, #12",
          "builder: payload at hdr+12", insn_at(ins, 0x571F4))
    check(insn_at(ins, 0x571F8) == "strh r8, [r0, #-4]",
          "builder: key u16 at hdr+20", insn_at(ins, 0x571F8))
    check(insn_at(ins, 0x571FC) == "strh r6, [r0, #-2]",
          "builder: len u16 at hdr+22", insn_at(ins, 0x571FC))

    # 3) the blob filler layout
    ins, _ = disasm_ko(od, wifi, FILLER, FILLER + 0x280)
    want = {
        0x121E24: "ldrsb r2, [r4, #12]",   # cc[0]
        0x121E2C: "strb r2, [r1]",         # blob[0]
        0x121E40: "ldrsb r2, [r4, #13]",   # cc[1]
        0x121E44: "strb r2, [r1, #1]",     # blob[1]
        0x121E48: "strb r3, [r1, #2]",     # blob[2] = 0
        0x121E94: "strb r3, [r5, #3]",     # chip id
        0x121E9C: "strb r3, [r5, #4]",     # count
        0x121F80: "strb lr, [r5, #8]",     # record byte0 = band (5G path)
        0x121FAC: "strb r3, [r5, #9]",     # record byte1 = bw code
        0x121FF8: "str r0, [r5, #20]",     # record bitmap (+0x0C rel)
        0x122000: "strb r3, [r5, #10]",    # record byte2 = 0 (never bit7)
    }
    for addr, exp in want.items():
        check(insn_at(ins, addr) == exp,
              "filler 0x%X: %s" % (addr, exp), insn_at(ins, addr))
    check(insn_at(ins, 0x122020) == "tst r3, #4",
          "filler: byte2 bit2 from next-block flags & 4", insn_at(ins, 0x122020))
    check(insn_at(ins, 0x122034) == "tst r3, #8",
          "filler: byte2 bit1 from next-block flags & 8", insn_at(ins, 0x122034))
    bit7_writers = [a for a, t in ins.items()
                    if a >= FILLER and a < FILLER + 0x280
                    and "[r5, #10]" in t and t.startswith("strb")]
    check(len(bit7_writers) == 1 and insn_at(ins, bit7_writers[0]) == "strb r3, [r5, #10]",
          "filler: the only write to record byte2 is a zero (no 0x80)")

    # 4) host twin: strcmp + memcpy_s with 0x148 bound
    ins, rel = disasm_ko(od, wifi, HOST_TWIN, HOST_TWIN + 0xD0)
    check(insn_at(ins, 0x206EC) == "ldr r5, [r0]",
          "host twin: blob = *cfg", insn_at(ins, 0x206EC))
    check(insn_at(ins, 0x206FC) == "ldrb r3, [r5, #4]",
          "host twin: count = blob[4]", insn_at(ins, 0x206FC))
    check(insn_at(ins, 0x20708) == "cmp r4, #328",
          "host twin: bound 0x148 = 8+20*16", insn_at(ins, 0x20708))
    check(any(t == "R_ARM_CALL" and s == "strcmp" for t, s in rel.get(0x2072C, [])),
          "host twin: strcmp(current, blob) at 0x2072C")
    check(any(t == "R_ARM_CALL" and s == "memcpy_s" for t, s in rel.get(0x2075C, [])),
          "host twin: memcpy_s(copy, 0x148, blob, len) at 0x2075C")
    ins, rel = disasm_ko(od, wifi, HOST_TWIN_MAP, HOST_TWIN_MAP + 0x100)
    check(insn_at(ins, 0x205FC) == "strb r2, [r3, #1]",
          "host twin map: clear band0 row slot to 0xFF", insn_at(ins, 0x205FC))
    check(insn_at(ins, 0x20658) == "ldr r2, [ip, #20]",
          "host twin map: band0 bitmap at record+0x0C", insn_at(ins, 0x20658))

    # 5) the two decoy emitters
    ins, rel = disasm_ko(od, wifi, 0x108400, 0x108470)
    check(insn_at(ins, 0x10843C) == "mov r2, #247",
          "decoy GET: wal_ioctl_get_reg_domain g2 cmd 0xF7")
    ins, rel = disasm_ko(od, wifi, WAL_UPDATE, WAL_UPDATE + 0x430)
    check(insn_at(ins, 0x1228F8) == "mov r2, #247",
          "decoy: wal_regdomain_update g2 cmd 0xF7", insn_at(ins, 0x1228F8))
    check(insn_at(ins, 0x1228FC) == "movt r2, #4",
          "decoy: its wrapper word = 0x000400F7 (host pointer payload)")
    check(any(t == "R_ARM_CALL" and s == "wal_send_cfg_event"
              for t, s in rel.get(WAL_UPDATE_SITE, [])),
          "decoy: wal_regdomain_update sends via wal_send_cfg_event (g2)")
    txt = run([od, "-d", "-r", "--no-show-raw-insn", plat])
    check(re.search(r"#247\b", txt) is None,
          "plat.ko contains no 0xF7 message key")
    ins, rel = disasm_ko(od, wifi, 0x63C30, 0x63C48)
    check(insn_at(ins, EMIT_SITE).startswith("bl 5717c"),
          "emitter: the 0xF7 call is the hmac constructor", insn_at(ins, EMIT_SITE))

    # 6) firmware: g0-id1 handler -> body = msg+24
    ins = disasm_bin(od, fw, HANDLER_G0ID1, HANDLER_G0ID1 + 0xC0)
    check(insn_at(ins, 0x43D90) == "ldrh r2, [r4, #20]",
          "handler: key = u16 [msg+20]", insn_at(ins, 0x43D90))
    check(insn_at(ins, 0x43DA2) == "cmp.w r3, #1736",
          "handler: table A bound 217*8 = 0x6C8", insn_at(ins, 0x43DA2))
    check(insn_at(ins, 0x43DCE) == "ldr r3, [r1, #4]",
          "handler: worker = entry+4", insn_at(ins, 0x43DCE))
    check(insn_at(ins, 0x43DEC) == "add.w r2, r4, #24",
          "handler: body = msg+24", insn_at(ins, 0x43DEC))
    check(insn_at(ins, 0x43DF0) == "ldrb r1, [r4, #22]",
          "handler: r1 = len low byte", insn_at(ins, 0x43DF0))
    check(insn_at(ins, 0x43DF2) == "blx r3", "handler: call worker")
    check(u32(img, 0x43E18) == TABLE_A,
          "handler: table A literal = CA 0x%X" % TABLE_A,
          hex(u32(img, 0x43E18)))

    # 7) table A row 92 = key 0xF7 -> worker 0x45A31
    entry = TABLE_A + 8 * ROW_0F7
    check(u16(img, entry) == 0x00F7,
          "table A row %d key = 0x00F7" % ROW_0F7, hex(u16(img, entry)))
    check(u32(img, entry + 4) == WORKER_0F7 | 1,
          "table A row %d worker = CA 0x%X (Thumb)" % (ROW_0F7, WORKER_0F7),
          hex(u32(img, entry + 4)))

    # 8) the 0xF7 worker
    ins = disasm_bin(od, fw, WORKER_0F7, WORKER_0F7 + 0xC0)
    check(insn_at(ins, 0x45A32) == "mov r1, r2",
          "worker: gate gets (pool, body)", insn_at(ins, 0x45A32))
    check("1037bc" in insn_at(ins, 0x45A3C),
          "worker: blx CA 0x1037BC (ARM state)", insn_at(ins, 0x45A3C))
    check(insn_at(ins, 0x45A40) == "cbz r0, 0x45a76",
          "worker: gate==0 -> skip the copy, still populate", insn_at(ins, 0x45A40))
    check(insn_at(ins, 0x45A44) == "ldrb r2, [r7, #4]",
          "worker: count = body[4]", insn_at(ins, 0x45A44))
    check(insn_at(ins, 0x45A46) == "mov.w r1, #328",
          "worker: destMax 0x148", insn_at(ins, 0x45A46))
    check(insn_at(ins, 0x45A4A) == "add.w r3, r3, r2, lsl #4",
          "worker: len = 8 + count*16", insn_at(ins, 0x45A4A))
    check(insn_at(ins, 0x45A52) == "bl 0xc1e44",
          "worker: memcpy_s(pool, 0x148, body, len)", insn_at(ins, 0x45A52))
    check(insn_at(ins, 0x45A6E) == "movw r0, #35629",
          "worker: refusal code 0x8B2D", insn_at(ins, 0x45A6E))
    check(insn_at(ins, 0x45A76) == "bl 0x1017f8",
          "worker: populator CA 0x1017F8", insn_at(ins, 0x45A76))
    check(insn_at(ins, 0x45A7A) == "ldr.w r6, [r4, #1208]",
          "worker: band object = [ctx+0x4B8]", insn_at(ins, 0x45A7A))
    check(u32(img, 0x45B40) == POOL,
          "worker: pool literal CA 0x%X" % POOL, hex(u32(img, 0x45B40)))

    # 9) the gate CA 0x1037BC = ARM strcmp
    ins = disasm_bin(od, fw, GATE, GATE + 0x98, thumb=False)
    want = {
        0x1037BC: "eor ip, r0, r1",
        0x1037C0: "tst ip, #3",
        0x1037C4: "bne 0x103850",
        0x1037C8: "ands ip, r0, #3",
        0x1037D4: "ldr r2, [r0], #4",
        0x1037FC: "push {r4}",
        0x103800: "mov r4, #1",
        0x103804: "orr r4, r4, r4, lsl #8",
        0x103808: "orr r4, r4, r4, lsl #16",
        0x103810: "cmp r2, r3",
        0x103840: "and r3, r3, #255",
        0x10384C: "bx lr",
    }
    for addr, exp in want.items():
        check(insn_at(ins, addr) == exp, "gate 0x%X: %s" % (addr, exp),
              insn_at(ins, addr))
    check(insn_at(ins, 0x103828) == "lsl r0, r2, #24",
          "gate: byte-tail compare (strcmp)", insn_at(ins, 0x103828))

    # 10) boot default pool builder CA 0x485BA..0x48620
    ins = disasm_bin(od, fw, 0x485F0, 0x48630)
    want = {
        0x485FA: "movs r3, #2",
        0x48602: "movs r2, #30",
        0x48604: "movw ip, #8191",
        0x48608: "movw r1, #513",
        0x4861E: "str r4, [r7, #8]",
        0x48620: "bl 0x1017f8",
    }
    for addr, exp in want.items():
        check(insn_at(ins, addr) == exp, "boot pool 0x%X: %s" % (addr, exp),
              insn_at(ins, addr))
    check(u32(img, 0x48694) == POOL, "boot pool: pool literal CA 0x%X" % POOL)
    check(u32(img, 0x48698) == BOOT_CC_LITERAL,
          "boot pool: cc literal CA 0x%X" % BOOT_CC_LITERAL)
    check(img[BOOT_CC_LITERAL - 0x40000:BOOT_CC_LITERAL - 0x40000 + 3] == BOOT_CC,
          "boot pool: default cc string '99' (the gate baseline)")
    want = {
        0x4860C: "strh r3, [r7, #14]",
        0x4860E: "strh r3, [r7, #30]",
        0x48610: "mvn.w r3, #3758096384",
        0x48614: "strb r2, [r7, #12]",
        0x48616: "strd ip, r1, [r7, #20]",
        0x4861A: "strb r2, [r7, #28]",
        0x4861C: "str r3, [r7, #36]",
    }
    for addr, exp in want.items():
        check(insn_at(ins, addr) == exp, "boot pool 0x%X: %s" % (addr, exp),
              insn_at(ins, addr))

    # 11) the predicate CA 0x101890 reads record byte2 bit7
    ins = disasm_bin(od, fw, PREDICATE, PREDICATE + 0x360)
    want = {
        0x101894: "cmp r0, #1",
        0x1018AC: "bl 0x10172c",
        0x10192C: "bl 0x101650",
        0x101932: "ldrb r3, [r0, #2]",
        0x101934: "lsls r3, r3, #31",
        0x101936: "bmi.w 0x101bd8",
        0x101BD8: "movs r0, #1",
    }
    for addr, exp in want.items():
        check(insn_at(ins, addr) == exp, "predicate 0x%X: %s" % (addr, exp),
              insn_at(ins, addr))
    check(u32(img, 0x101BA0) == 0x10BE68,
          "predicate: field table literal CA 0x10BE68", hex(u32(img, 0x101BA0)))

    # 12) the fix frame
    frame, blob, total = fix_frame()
    check(frame.hex() == FRAME_HEX, "frame = the committed 68-byte image",
          frame.hex())
    check(len(frame) == 68 and total == 0x44, "frame total 0x44")
    check((frame[20] | (frame[21] << 8)) == 0x00F7, "frame key at +0x14 = 0x00F7")
    check((frame[6] | (frame[7] << 8)) == 1 and frame[0] == 0 and frame[10:12] == b"\x5a\x5a",
          "frame is g0 id1 with tag 0x5A5A")
    check(frame[4] | (frame[5] << 8) == len(frame), "hdr total == wire length")
    check(blob[0:2] == CC_FIX and blob[4] == 2, "blob cc != '99', count 2")
    word1 = struct.unpack_from("<I", blob, 8 + 16 + 0)[0]
    check(word1 == 0x00800201, "rec1 word 0x00800201 (band1, bw20, valid)",
          hex(word1))
    check(word1 & 0x00800000, "rec1 valid bit 0x00800000 set")
    bm1 = struct.unpack_from("<I", blob, 8 + 16 + 12)[0]
    check(bm1 == BAND1_BITMAP_CH36_64, "rec1 bitmap = ch36-64 (0x%X)" % bm1)
    check(8 + 16 * blob[4] <= DESC_MAX, "worker length within the 0x148 pool window")
    check(((len(frame) << 16) | 0x6D2B) == 0x00446D2B, "SR node word1 0x00446D2B")

    print()
    print("%d checks, %d failed" % (len(CHECKS), len(FAILS)))
    if FAILS:
        for f in FAILS:
            print("  FAILED: " + f)
        return 1
    print("SELFTEST PASS")
    return 0


def emit_frame():
    frame, blob, total = fix_frame()
    print("blob (%d B): %s" % (len(blob), blob.hex()))
    print("frame (%d B): %s" % (len(frame), frame.hex()))
    print("hdr total=0x%04X id=0x0001 group=0 tag=0x5A5A" % total)
    print("key=0x00F7 payload_len=0x%04X count=%d" % (len(blob), blob[4]))
    print("rec1 word=0x00800201 bitmap=0x%08X (ch36..64)"
          % BAND1_BITMAP_CH36_64)
    print("cc=" + CC_FIX.decode() + " (must differ from boot '99')")
    print("SR node word1=0x%08X" % (((len(frame) << 16) | 0x6D2B)))
    return 0


def emit_tables():
    print("T2 frame: group 0 id 1, key u16 @hdr+20, len u16 @hdr+22,")
    print("          blob @hdr+24, total = len + 28, tag 0x5A5A")
    print("T3 blob : cc[2] NUL chip_id count=u8@+4  ->  N x 16 B records")
    print("          rec: u32 word @+0 | u8 @+4 | u16 @+6 | u32 bitmap @+12")
    print("          word byte0 band(0=2.4G,1=5G) byte1 bw(2/4/5) byte2 bit7 valid")
    print("          N <= %d (0x148 = 8 + 20*16)  no CRC" % N_MAX)
    print("T5 worker CA 0x%X: gate CA 0x%X -> memcpy_s CA 0x%X -> populator CA 0x%X"
          % (WORKER_0F7, GATE, MEMCPY_S, POPULATOR))
    print("T5 gate CA 0x%X = ARM strcmp(pool, body): install iff cc differs"
          % GATE)
    print("T5 predicate CA 0x%X: band1 only, returns byte2 bit7; latch CA 0x%X"
          % (PREDICATE, LATCH))
    print("T4 vendor emitter .text+0x%X -> site 0x%X -> builder .text+0x%X"
          % (EMITTER, EMIT_SITE, BUILDER))
    print("T6 boot pool: cc '99', count 2, rec1 word 0x00000201 (bit7 clear)")
    return 0


def emit_probes():
    def phys(ca):
        return 0x40000000 + ca + 0x6B8000
    rows = [
        ("pool header cc+count", POOL, 8),
        ("pool rec0 word", POOL + 8, 4),
        ("pool rec1 word", POOL + 24, 4),
        ("pool rec1 bitmap", POOL + 36, 4),
        ("band1 map ch36 row", MAP_B1, 4),
        ("band0 map ch1 row", MAP_B0, 4),
    ]
    print("%-24s %-10s %-10s %-10s" % ("object", "CPU", "BAR0", "phys"))
    for name, ca, ln in rows:
        print("%-24s 0x%08X 0x%08X 0x%08X  len %d"
              % (name, ca, ca + 0x6B8000, phys(ca), ln))
    print("R chain: ctx0=u32(0x171458)+u16(0x172504); perlink=u32(ctx0+0x4B0);")
    print("         O=u32(perlink+0x2820); R=u32(O+0x2D8); read R+0x410/R+0x3AC")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", default=FW_DEFAULT)
    ap.add_argument("--ko-dir", default=KO_DIR)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--frame", action="store_true")
    ap.add_argument("--tables", action="store_true")
    ap.add_argument("--probes", action="store_true")
    args = ap.parse_args()
    od = find_tool("FW_DESC_TABLE_OD", OD_DEFAULT, OD_LOCAL)
    nm = find_tool("FW_DESC_TABLE_NM", NM_DEFAULT, NM_LOCAL)
    if not od or not nm:
        sys.exit("error: ARM binutils not found (set FW_DESC_TABLE_OD/NM)")
    rc = 0
    if args.frame:
        rc |= emit_frame()
    if args.tables:
        rc |= emit_tables()
    if args.probes:
        rc |= emit_probes()
    if args.selftest or not (args.frame or args.tables or args.probes):
        rc |= selftest(od, nm, args.fw, args.ko_dir)
    return rc


if __name__ == "__main__":
    sys.exit(main())
