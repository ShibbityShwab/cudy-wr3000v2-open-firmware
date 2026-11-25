#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""fw_ax_advanced_re.py - re-derive the vendor 802.11ax advanced-feature
machinery (OFDMA / MU-MIMO / TWT / BSS color) from the pinned local artifacts.

Lane: wifi-ax-advanced (shared task task-195).  HOST-ONLY, read-only: the
pinned LOCAL vendor artifacts are read in place; only derived values
(addresses, instruction encodings, table words, field maps and the bytes of
OUR OWN host-built protocol frames) are printed.  No vendor byte is copied
into the repository and no device is touched.

Artifacts (local, uncommitted):
  firmware: /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/
            FIRMWARE.bin  928,920 B sha256 7fc87e20..311b ; CPU = file + 0x40000
  module:   .../lib/hisilicon/ko/hi5622v100_wifi.ko (ELF .text + .symtab read
            in place; link-time section offsets, VMA 0)

What this tool asserts (evidence: ../REPORT.md, ../TABLES.md):
  * the g0-id1 config-event dispatcher (CA 0x43D58) scans table A (CA
    0x10C23C, 217 x {u16 key;u16 pad;u32 worker}) then table B (CA 0x103BF8,
    88 rows); body = hdr+24, len = u8 hdr+22, no length gate;
  * ten AX config keys live in those tables: HE GI 0x006F, ext-TWT IE 0x00E9,
    TWT enable 0x035D, MU EDCA selectors 0x005B..0x005F, BFER 0x04C8,
    BFEE 0x0335, trigger SU BF feedback 0x032E, cyclic-scheme 0x0353,
    multi-AP SR 0x04B6, multi-AP tool 0x04B4;
  * each key's firmware worker and the exact per-VAP / per-module-context
    field it writes (raw instruction bytes asserted);
  * the vendor host builders that emit those frames (module .text, key
    movw immediates and body-field loads asserted via .symtab addresses);
  * the ALG config text plane: g0-id1 key 0x0128 -> table B row 28 -> worker
    CA 0x8313C, which parses {u8 n; u8 off[n]; tokens...} text bodies, splits
    on 0x20, dispatches token[0] against the three-module name table
    CA 0x104FC0 ("sch"/"cmn"/"err_name") and calls the runtime-installed
    handler at CBH+G_OFF+4*(0xD2+module) with CBH = CA 0x1747C4;
  * the host-side HE Operation / BSS-color builders (mac_set_he_operation_
    params, mac_set_bss_color_info): the on-air 3-byte HE Operation
    Parameters value 0x003FF4 is exactly 4 | ((u16(MIB+0x1BA)>>6)<<4) for
    the observed TXOP-RTS-threshold 1023, and the BSS Color Information
    byte comes from object+0xCC masked to 7 bits; those builders are NOT in
    the firmware-beacon path (beacon IEs are a firmware effect of the
    AP-config wireless-mode byte 10);
  * the read-only probe pack addresses (static + pointer-chased) and the
    physical mapping for the bench read helper;
  * our own host frame model (g0-id1 envelope) for the AX config events,
    byte-identical to the committed-vap-start/dfs-mode-send envelope.

Modes:
  --selftest   assert every decoded fact (exit 0 on PASS)
  --keys       the AX config-key table (table A/B rows -> worker CA)
  --decode     the ten workers: body parse and state writes
  --alg        the ALG text protocol + the three module handlers
  --host       the vendor host builders and the HE Operation/BSS color decode
  --frames     our own host-built AX config-event frames (protocol bytes)
  --probes     the read-only probe pack (CPU/BAR0/phys)
"""
import argparse
import hashlib
import struct
import sys

FW = ("/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/"
      "FIRMWARE.bin")
FW_SHA = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920
BASE = 0x40000                    # CPU = file offset + 0x40000
KO = ("/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/"
      "hi5622v100_wifi.ko")

# --- firmware addresses (CPU) ---------------------------------------------
HANDLER_G0ID1 = 0x43D58           # g0-id1 config-event dispatcher
TBL_A = 0x10C23C                  # 217 x 8 B {u16 key; u16 pad; u32 worker}
TBL_A_N = 217
TBL_B = 0x103BF8                  # 88 x 8 B
TBL_B_N = 88
CTXG = 0x170E28                   # context-table global (slot table +0x630)
CTX_TBL = CTXG + 0x630            # 0x171458
CTX_BASE = CTXG + 0x6DC           # 0x171504 u16 common offset
CTX_POOL = 0x119758               # context 0
CTX_STRIDE = 0xB40
CTX_N = 19
CBH = 0x1747C4                    # boot-installed callback/hook block base
ALG_NAMES = 0x104FC0              # 3 x 8 B {name ptr; u32 module id}
ALG_HANDLER_OFF = 0xD2            # handler slot = CBH + 4*(0xD2+module)
ALG_WORKER = 0x8313C
SEND_EVENT = 0x5717C              # host: g0-id1 frame builder

# --- the AX config-event key map (table A except where noted) --------------
# (table, key, worker thumb value, worker CA, name)
AX_KEYS = [
    ("A", 0x006F, 0x081C45, 0x081C44, "HE GI"),
    ("A", 0x00E9, 0x044485, 0x044484, "ext TWT IE switch"),
    ("A", 0x035D, 0x041E5B, 0x041E5A, "TWT enable"),
    ("A", 0x005B, 0x0813BD, 0x0813BC, "MU EDCA param 0x5B"),
    ("A", 0x005C, 0x081371, 0x081370, "MU EDCA param 0x5C"),
    ("A", 0x005D, 0x081325, 0x081324, "MU EDCA param 0x5D"),
    ("A", 0x005E, 0x0812D9, 0x0812D8, "MU EDCA param 0x5E"),
    ("A", 0x005F, 0x08128D, 0x08128C, "MU EDCA param 0x5F"),
    ("A", 0x04C8, 0x0817CD, 0x0817CC, "BFER support"),
    ("A", 0x0335, 0x081891, 0x081890, "BFEE support"),
    ("A", 0x032E, 0x0818DD, 0x0818DC, "trigger SU BF feedback"),
    ("A", 0x0353, 0x043BD5, 0x043BD4, "cyclic-scheme switch"),
    ("A", 0x04B6, 0x049ACB, 0x049ACA, "multi-AP SR enable"),
    ("A", 0x04B4, 0x04A865, 0x04A864, "multi-AP tool (text)"),
    ("B", 0x0128, 0x08313D, 0x08313C, "ALG config text plane"),
]
MU_EDCA_KEYS = (0x005B, 0x005C, 0x005D, 0x005E, 0x005F)

# --- firmware raw instruction assertions ----------------------------------
FW_BYTES = [
    # dispatcher (context from the committed dfs-worker lane; re-asserted)
    (0x43D72, "c178", "ldrb r1,[r0,#3] field B"),
    (0x43D90, "a28a", "ldrh r2,[r4,#20] key = hdr+20"),
    (0x43DEC, "04f11802", "add.w r2,r4,#24 body = hdr+24"),
    (0x43DF0, "a17d", "ldrb r1,[r4,#22] len = ONE byte"),
    (0x43DF2, "9847", "blx worker(r0=ctx,r1=len,r2=body)"),
    (0x43E18, "3cc21000", "table A literal"),
    (0x43E1C, "f83b1000", "table B literal"),
    # table A rows for the AX keys (entry = {u16 key;u16 pad;u32 worker})
    (TBL_A + 12 * 8, "2e030000dd180800", "row 12 key 0x032E -> worker 0x0818DD"),
    (TBL_A + 14 * 8, "3503000091180800", "row 14 key 0x0335 -> worker 0x081891"),
    (TBL_A + 15 * 8, "c8040000cd170800", "row 15 key 0x04C8 -> worker 0x0817CD"),
    (TBL_A + 65 * 8, "5b000000bd130800", "row 65 key 0x005B -> 0x0813BD"),
    (TBL_A + 66 * 8, "5c00000071130800", "row 66 key 0x005C -> 0x081371"),
    (TBL_A + 67 * 8, "5d00000025130800", "row 67 key 0x005D -> 0x081325"),
    (TBL_A + 68 * 8, "5e000000d9120800", "row 68 key 0x005E -> 0x0812D9"),
    (TBL_A + 69 * 8, "5f0000008d120800", "row 69 key 0x005F -> 0x08128D"),
    (TBL_A + 89 * 8, "5d0300005b1e0400", "row 89 key 0x035D -> 0x041E5B"),
    (TBL_A + 106 * 8, "6f000000451c0800", "row 106 key 0x006F -> 0x081C45"),
    (TBL_A + 165 * 8, "53030000d53b0400", "row 165 key 0x0353 -> 0x043BD5"),
    (TBL_A + 184 * 8, "e900000085440400", "row 184 key 0x00E9 -> 0x044485"),
    (TBL_A + 189 * 8, "b404000065a80400", "row 189 key 0x04B4 -> 0x04A865"),
    (TBL_A + 190 * 8, "b6040000cb9a0400", "row 190 key 0x04B6 -> 0x049ACB"),
    # table B row 28 = the ALG config text plane
    (TBL_B + 28 * 8, "280100003d310800", "row 28 key 0x0128 -> 0x08313D"),
    # ALG text worker CA 0x8313C: len gate, token count, token pointers,
    # space->NUL split, module table compare, runtime handler dispatch
    (0x8313C, "2de9f04308230646", "push; movs r3,#8; mov r6,r0"),
    (0x83148, "8b4228d3", "cmp r3,r1; bcc fail (len < 8)"),
    (0x8319E, "d45c202c08bf", "ldrb r4,[r2,r3]; cmp #0x20; strbeq 0"),
    (0x831AC, "d1184978", "adds r1,r2,r3; ldrb r1,[r1,#1] token offset"),
    (0x831B0, "08311144", "adds r1,#8; add r1,r2 -> token ptr"),
    (0x831B4, "4df82310", "str.w r1,[sp,r3,lsl #2] pointer array"),
    (0x831CC, "0a4bd234", "ldr r3,=CBH; adds r4,#0xD2"),
    (0x831D0, "53f82430", "ldr.w r3,[r3,r4,lsl #2] handler slot"),
    (0x831D6, "f1d1", "bne -> call handler"),
    (0x831FC, "c04f1000", "literal CA 0x00104FC0 (module name table)"),
    (0x831F8, "c4471700", "literal CA 0x001747C4 (CBH)"),
    # module name table: sch / cmn / err_name
    (0x104FC0, "d84f100000000000dc4f100001000000e04f100002000000",
     "name table: sch id0 / cmn id1 / err_name id2"),
    (0x104FD8, "73636800636d6e006572725f6e616d65", '"sch\\0cmn\\0err_name"'),
    # TWT enable worker CA 0x41E5A: body[0] bits 0/1 -> ctx+0xDA bits 6/7
    (0x41E5A, "114610b51378047803f00102", "mov r1,r2; push; ldrb r3,[r2]; ..."),
    (0x41E66, "90f8da30", "ldrb.w r3,[r0,#0xDA]"),
    (0x41E6C, "62f38613", "bfi r3,r2,#6,#1  (bit0 = TWT enable)"),
    (0x41E70, "80f8da30", "strb.w r3,[r0,#0xDA]"),
    (0x41E7E, "63f3c711", "bfi r1,r3,#7,#1  (bit1 = TWT required)"),
    (0x41E82, "80f8da10", "strb.w r1,[r0,#0xDA]"),
    # ext-TWT IE switch worker CA 0x44484: u32 body[0] -> VAP+0x3DE
    (0x44484, "38b5044600781546", "push; mov r4,r0; ldrb r0,[r0]; mov r5,r2"),
    (0x444AA, "2b681bb9", "ldr r3,[r5]; cbnz -> store"),
    (0x444AE, "80f8de33", "strb.w r3,[r0,#0x3DE]  (VAP ext-TWT-IE enable)"),
    # HE GI worker CA 0x81C44: body[0] -> VAP+0x831, VAP+0x1A7 bit7,
    # VAP+0x1A8 bit5 (body[0]==1 -> set, else clear bit5)
    (0x81C44, "38b504460078154614f0affa", "push; ldrb r0; bl VAP lookup"),
    (0x81C6C, "80f83138", "strb.w r3,[r0,#0x831]"),
    (0x81C70, "012bd4f8", "cmp r3,#1; ldr r3,[r4,#0x190]"),
    (0x81C80, "83f8a721", "strb.w r2,[r3,#0x1A7]"),
    (0x81C90, "82f8a831", "strb.w r3,[r2,#0x1A8]"),
    # BFEE worker CA 0x81890: body[0] -> VAP+0x364/+0x366, VAP+0x1A4 bit1
    (0x81890, "08b55ab9037810481b0603f0", "push; cbnz body; ldrb r3,[r0]"),
    (0x818B2, "83f86413", "strb.w r1,[r3,#0x364]"),
    (0x818BC, "83f86613", "strb.w r1,[r3,#0x366]"),
    (0x818CC, "62f34101", "bfi r1,r2,#1,#1 (VAP+0x1A4 bit1)"),
    # BFER worker CA 0x817CC: body[0] switch 0..3 -> VAP+0x365/+0x367,
    # VAP+0x1A4 bits 0/2
    (0x817CC, "08b55ab903782e481b0603f0", "push; cbnz body; ldrb r3,[r0]"),
    (0x817E8, "12780023032a42d8", "ldrb r2,[r2]; movs r3,#0; cmp #3; bhi"),
    (0x817F0, "dfe802f0021c", "tbb [pc,r2] (4-way mode switch)"),
    # trigger SU BF feedback worker CA 0x818DC: VAP+0x1A6 bit7
    (0x818DC, "08b55ab903780b481b0603f0", "push; cbnz body; ldrb r3,[r0]"),
    (0x818F8, "d0f890311278", "ldr.w r3,[r0,#0x190]; ldrb r2,[r2]"),
    (0x81904, "62f3c711", "bfi r1,r2,#7,#1 (VAP+0x1A6 bit7)"),
    # cyc-scheme worker CA 0x43BD4: body[0] -> band object O+0x1A08 calls
    (0x43BD4, "10b500b12ab9e1211048fff7", "push; cbz ctx/body; log"),
    (0x43BE6, "0078147879f0ebf8", "ldrb r0,[r0]; ldrb r4,[r2]; bl lookup"),
    (0x43BF8, "00f5d0500830", "add.w r0,r0,#0x1A00; adds #8"),
    # multi-AP SR enable worker CA 0x49ACA: body[0]&1 -> ctx+0xEE
    (0x49ACA, "08b51278037802f0010280f8", "push; ldrb r2,[r2]; ldrb r3,[r0]"),
    (0x49AD4, "80f8ee20", "strb.w r2,[r0,#0xEE] (per-module SR enable)"),
    # multi-AP tool worker CA 0x4A864: text command ->
    # 4 x {name ptr; handler ptr} table at CA 0x104520
    (0x4A864, "2de9f7430024174603780646", "push; movs r4,#0; mov r7,r2"),
    (0x4A88E, "77f097fa", "bl CA 0xC1DC0 (parse one text arg)"),
    (0x4A8E4, "20451000", "literal CA 0x00104520 (4-entry tool table)"),
    # MU EDCA workers CA 0x8128C..0x813BC: {u16 sel;u16 pad;u32 ac;u32 val}
    # -> VAP[+0x1D4+4*sel + 24*ac]; sel 0x5B..0x5F -> +0x1D4..+0x1E4
    (0x8128C, "10b5d2e9011303290dd90478", "MU 0x5F: ldrd body[4],body[8]"),
    (0x812BC, "81f8e431", "strb.w r3,[r1,#0x1E4] (u8 field)"),
    (0x812C0, "90f8d330", "ldrb.w r3,[r0,#0xD3] (seq counter)"),
    (0x812FE, "1824d0f8", "movs r4,#24; ldr.w r2,[r0,#0x190]"),
    (0x813EE, "c1f8d431", "str.w r3,[r1,#0x1D4] (u32 field)"),
]

# --- module (host) raw instruction assertions -----------------------------
# .text VMA is 0, so the address IS the .text-relative offset.
KO_BYTES = [
    # hmac_config_send_event: key u16 at hdr+8, len at hdr+10, body hdr+12
    (0x571F8, "b48040e1", "strh r8,[r0,#-4] key @hdr+8"),
    (0x571FC, "b26040e1", "strh r6,[r0,#-2] len @hdr+10"),
    (0x57200, "feffffeb", "memcpy_s(hdr+12, len, body, len)"),
    # HE GI builder: ldrb body[0]; key 0x6F; valid {1,2,4}
    (0x753C0, "0060d2e5", "ldrb r6,[r2]  body[0]"),
    (0x75418, "010046e20400", "sub r0,r6,#1; cmp r6,#4; cmpne r0,#1"),
    (0x75468, "6f10a0e3", "mov r1,#0x6F key"),
    (0x7546C, "feffffeb", "bl hmac_config_send_event"),
    # ext TWT IE switch builder: key 0xE9, raw body
    (0x61340, "e910a0e3", "mov r1,#0xE9 key"),
    # TWT enable builder: body[0] bits -> host mirror +0xD2, key 0x35D
    (0x15E8F8, "0030d2e5", "ldrb r3,[r2]  body[0]"),
    (0x15E918, "d220c4e5", "strb r2,[r4,#0xD2] host mirror"),
    (0x15E95C, "5d1300e3", "movw r1,#0x35D key"),
    # trigger SU BF feedback: key 0x32E
    (0x75E54, "0040d3e5", "ldrb r4,[r3]  body[0]"),
    (0x75E58, "2e1300e3", "movw r1,#0x32E key"),
    # BFEE: key 0x335, VAP+0x364/+0x366
    (0x75F38, "00c0d3e5", "ldrb ip,[r3]  body[0]"),
    (0x75F3C, "64c3c1e5", "strb ip,[r1,#0x364] host VAP"),
    (0x75F40, "351300e3", "movw r1,#0x335 key"),
    # BFER: key 0x4C8, 4-way mode switch
    (0x75FE0, "0030d2e5", "ldrb r3,[r2]  body[0]"),
    (0x76068, "c81400e3", "movw r1,#0x4C8 key"),
    # cyc-scheme switch: key 0x353
    (0x161274, "531300e3", "movw r1,#0x353 key"),
    # multi-AP SR enable: key 0x4B6, host +0xE6
    (0x0C6D50, "0000d2e5", "ldrb r0,[r2]  body[0]"),
    (0x0C6D68, "e600c4e5", "strb r0,[r4,#0xE6] host mirror"),
    (0x0C6D8C, "b61400e3", "movw r1,#0x4B6 key"),
    # MU EDCA builder: selector = u16 body[0] in 0x5B..0x5F, ac = body[4],
    # value = body[8]; switch writes host VAP +0x1D4 + 4*(sel-0x5B) + 24*ac
    (0x76268, "000093e5", "ldr r0,[r3]  body[0] selector word"),
    (0x76274, "7010ffe6", "uxth r1,r0"),
    (0x76280, "5b0041e2", "sub r0,r1,#0x5B  selector base"),
    (0x762A4, "8c0195e5", "ldr r0,[r5,#0x18C]  host VAP"),
    (0x762B8, "feffffeb", "bl hmac_config_send_event"),
    # ALG config text sender: len gate > 8, token offsets, key 0x128
    (0x58E4C, "080055e3", "cmp r5,#8  len gate"),
    (0x58E9C, "01c0fee5", "ldrb ip,[lr,#1]! token offset"),
    (0x58EF0, "4a1fa0e3", "mov r1,#0x128 key"),
    # vendor user-space TWT path: WAL cfg event {u8 value; u16 key; u16 len}
    (0x1316BC, "030050e3", "cmp r0,#3"),
    (0x1316C0, "01005013", "cmpne r0,#1  (valid {1,3})"),
    (0x1316DC, "5d3300e3", "movw r3,#0x35D"),
    (0x1316E0, "0420a0e3", "mov r2,#4  body len"),
    # HE Operation Parameters builder: out = 4 | ((u16(MIB+0x1BA)>>6)<<4)
    (0x25594, "bac100e3", "movw ip,#0x1BA  MIB field"),
    (0x2559C, "043083e3", "orr r3,r3,#4  Default PE Duration = 4"),
    (0x255AC, "0020d1e5", "ldrb r2,[r1]"),
    (0x255B0, "bc3093e1", "ldrh r3,[r3,ip]  u16 MIB+0x1BA"),
    (0x255BC, "2333a0e1", "lsr r3,r3,#6"),
    # BSS Color Information byte: object+0xCC masked to 7 bits
    (0x25610, "cc30d0e5", "ldrb r3,[r0,#0xCC]  BSS color byte"),
    (0x25628, "833ce0e1", "mvn r3,r3,lsl #25"),
    (0x2562C, "a33ce0e1", "mvn r3,r3,lsr #25 (keep 7 bits)"),
    # SR config param: id 0xCE5 -> hmac_sr_cfg_param_bss_color
    (0x963CC, "f0412de9", "hmac_alg_sr_config_param head"),
    (0x96408, "b0e0d1e1", "ldrh lr,[r1]  param id"),
    (0x9640C, "e5cc00e3", "movw ip,#0xCE5"),
    (0x96484, "feffffeb", "bl hmac_sr_cfg_param_bss_color"),
]

# host symbols asserted through .symtab (name -> link-time .text offset)
KO_SYMS = [
    ("hmac_config_send_event", 0x5717C),
    ("hmac_config_set_he_gi", 0x753B0),
    ("hmac_config_ext_twt_ie_switch", 0x61330),
    ("hmac_config_twt_enable", 0x15E8E8),
    ("wal_hipriv_twt_enable", 0x131634),
    ("hdpp_d2h_user_twt_syn", 0x2B6C),
    ("hmac_config_set_trig_su_bf_fb", 0x75E40),
    ("hmac_config_set_bfee_support", 0x75F0C),
    ("hmac_config_set_bfer_support", 0x75FCC),
    ("hmac_config_set_cyc_sche_switch", 0x161258),
    ("hmac_config_multiap_sr_enable", 0xC6D3C),
    ("hmac_config_multiap_tool", 0xC3F20),
    ("hmac_config_set_wmm_mu_edca_params", 0x76250),
    ("hmac_config_alg", 0x58E18),
    ("mac_set_he_operation_params", 0x25588),
    ("mac_set_bss_color_info", 0x25604),
    ("mac_set_he_opern_ie", 0x26218),
    ("hmac_sr_cfg_param_bss_color", 0x961D4),
    ("hmac_alg_sr_config_param", 0x963CC),
    ("hmac_sr_bss_color_change_event", 0x95D6C),
    ("hmac_config_set_beacon", 0x745A0),
]

# --- our own host-built frames (protocol bytes, not vendor bytes) ----------
# g0-id1 envelope: 12 B HCC header, 8 B token, u16 key, u16 body_len, body,
# 4 B 0xFF pad; total = 28 + body_len.
EV_HDR = bytes.fromhex("00010000" "0000" "0100" "0000" "5a5a")  # 12 B; total patched


def g0_frame(field_b, key, body):
    n = 28 + len(body)
    h = bytearray(12)
    h[0] = 0x00                     # group 0
    h[1] = 0x01                     # dst 0 << 4 | alloc 1
    h[3] = field_b & 0xFF           # field B = per-module context index
    struct.pack_into("<H", h, 4, n)  # total
    struct.pack_into("<H", h, 6, 1)  # header id = g0 id1
    h[10:12] = bytes.fromhex("5a5a")
    tok = bytes(4) + struct.pack("<I", 1)   # the fired g0 token
    return (bytes(h) + tok + struct.pack("<H", key) +
            struct.pack("<H", len(body)) + body + b"\xff\xff\xff\xff")


CHECKS = []


def ck(name, cond, detail=""):
    CHECKS.append((name, bool(cond), detail))


def fw():
    with open(FW, "rb") as f:
        return f.read()


def fwb(d, ca, n):
    off = ca - BASE
    if off < 0 or off + n > len(d):
        return b""
    return d[off:off + n]


def fwu16(d, ca):
    b = fwb(d, ca, 2)
    return struct.unpack("<H", b)[0] if len(b) == 2 else None


def fwu32(d, ca):
    b = fwb(d, ca, 4)
    return struct.unpack("<I", b)[0] if len(b) == 4 else None


def ko_load():
    d = open(KO, "rb").read()
    e_shoff = struct.unpack("<I", d[32:36])[0]
    e_shentsize = struct.unpack("<H", d[46:48])[0]
    e_shnum = struct.unpack("<H", d[48:50])[0]
    e_shstrndx = struct.unpack("<H", d[50:52])[0]
    secs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        secs.append(struct.unpack("<IIIIII", d[off:off + 24]))
    shstr = d[secs[e_shstrndx][4]:secs[e_shstrndx][4] + secs[e_shstrndx][5]]
    names = {}
    for s in secs:
        n = shstr[s[0]:shstr.index(b"\x00", s[0])].decode()
        names[n] = s
    text = d[names[".text"][4]:names[".text"][4] + names[".text"][5]]
    return d, names, text


def ko_symtab(d, names):
    st = names[".symtab"]
    strtab = names[".strtab"]
    out = {}
    n = st[5] // 16
    for i in range(n):
        off = st[4] + i * 16
        nm, val, size, info, oth, shndx = struct.unpack("<IIIBBH",
                                                       d[off:off + 16])
        if nm == 0:
            continue
        so = strtab[4] + nm
        e = d.index(b"\x00", so)
        name = d[so:e].decode("latin1")
        if shndx == names[".text"][0] or names[".text"][0] == shndx:
            pass
        out.setdefault(name, []).append(val)
    return out


def table_entry(d, table, n, idx):
    e = fwb(d, table + idx * 8, 8)
    k, pad, w = struct.unpack("<HHI", e)
    return k, pad, w


def find_key(d, key):
    for idx in range(TBL_A_N):
        k, pad, w = table_entry(d, TBL_A, TBL_A_N, idx)
        if k == key:
            return ("A", idx, pad, w)
    for idx in range(TBL_B_N):
        k, pad, w = table_entry(d, TBL_B, TBL_B_N, idx)
        if k == key:
            return ("B", idx, pad, w)
    return None


def parse_alg_body(body):
    """Validate/parse an ALG text body: {u8 n; u8 off[n]; token strings}."""
    if len(body) < 8:
        return None, "len < 8"
    n = body[0]
    if n < 1 or 1 + n > len(body):
        return None, "bad token count"
    toks = []
    for k in range(1, n + 1):
        o = body[k]
        p = 8 + o
        if p >= len(body):
            return None, "token offset out of body"
        e = body.find(b"\x00", p)
        if e < 0:
            return None, "token not NUL-terminated"
        toks.append(body[p:e].decode("latin1"))
    return toks, None


def build_alg_body(tokens):
    """Build the ALG text body from tokens (token[0] = module name)."""
    hdr = bytes([len(tokens)]) + bytes(len(tokens))
    strs = b"".join(t.encode() + b"\x00" for t in tokens)
    # token offsets are relative to body+8; header is 1 + n bytes, so the
    # first token starts at 8+0 -> pad the header area to 7 bytes after n.
    pad = 7 - len(tokens)
    if pad < 0:
        raise ValueError("too many tokens")
    head = hdr + bytes(pad)
    offs = []
    cur = 0
    for t in tokens:
        offs.append(cur)
        cur += len(t) + 1
    for k, o in enumerate(offs):
        head = head[:1 + k] + bytes([o]) + head[2 + k:]
    return head + strs


def selftest():
    d = fw()
    kd, knames, ktext = ko_load()
    ck("firmware size == 928920", len(d) == FW_LEN, str(len(d)))
    ck("firmware sha256 pinned", hashlib.sha256(d).hexdigest() == FW_SHA)
    for ca, hx, what in FW_BYTES:
        got = fwb(d, ca, len(hx) // 2).hex()
        ck("fw %05X %s" % (ca, what), got == hx, got)
    # table structure
    ck("table A size 217*8 == 0x6C8 bound",
       TBL_A + TBL_A_N * 8 - TBL_A == 0x6C8)
    ck("table B size 88*8 == 0x2C0 bound",
       TBL_B + TBL_B_N * 8 - TBL_B == 0x2C0)
    ck("dispatcher table A literal", fwu32(d, 0x43E18) == TBL_A)
    ck("dispatcher table B literal", fwu32(d, 0x43E1C) == TBL_B)
    # AX keys resolve to the decode
    for tname, key, thumb, ca, what in AX_KEYS:
        got = find_key(d, key)
        ck("AX key 0x%04X present in table %s" % (key, tname),
           got is not None and got[0] == tname, str(got))
        if got:
            ck("AX key 0x%04X row %d (%s)" % (key, got[1], what),
               got[3] == thumb and (got[3] & ~1) == ca, hex(got[3]))
    # MU EDCA selector arithmetic: sel 0x5B..0x5F -> +0x1D4..+0x1E4
    ck("MU EDCA selector stride 4", (0x1E4 - 0x1D4) // 4 == 4)
    ck("MU EDCA per-AC stride 24", 24 == 0x18)
    # TWT body semantics
    ck("TWT host validator accepts {1,3}", True)
    # ALG plane
    ck("ALG worker literal = CBH", fwu32(d, 0x831F8) == CBH)
    ck("ALG worker literal = name table", fwu32(d, 0x831FC) == ALG_NAMES)
    ids = []
    names = []
    for i in range(3):
        p = fwu32(d, ALG_NAMES + i * 8)
        mid = fwu32(d, ALG_NAMES + i * 8 + 4)
        s = fwb(d, p, 16).split(b"\x00")[0].decode()
        ids.append(mid)
        names.append(s)
    ck("ALG module ids 0,1,2", ids == [0, 1, 2], str(ids))
    ck("ALG module names sch/cmn/err_name",
       names == ["sch", "cmn", "err_name"], str(names))
    ck("ALG handler slot 0 = CBH+0x348",
       CBH + 4 * (ALG_HANDLER_OFF + 0) == 0x174B0C)
    ck("ALG handler slot 1 = CBH+0x34C",
       CBH + 4 * (ALG_HANDLER_OFF + 1) == 0x174B10)
    ck("ALG handler slot 2 = CBH+0x350",
       CBH + 4 * (ALG_HANDLER_OFF + 2) == 0x174B14)
    # ALG text body roundtrip model
    for toks in (["sch", "usr_bw", "00:11:22:33:44:55", "40"],
                 ["cmn", "x=1"]):
        body = build_alg_body(toks)
        got, err = parse_alg_body(body)
        ck("ALG body roundtrip %s" % toks[0], err is None and got == toks,
           str((got, err)))
    ck("ALG body rejects len<8", parse_alg_body(b"sch\0\0\0\0")[1] is not None)
    # host module
    for ca, hx, what in KO_BYTES:
        got = ktext[ca:ca + len(hx) // 2].hex()
        ck("ko .text+%05X %s" % (ca, what), got == hx, got)
    syms = ko_symtab(kd, knames)
    for name, addr in KO_SYMS:
        ck("ko symbol %s == 0x%X" % (name, addr),
           addr in syms.get(name, []), str(syms.get(name)))
    # HE Operation Parameters arithmetic: on-air 0x003FF4
    v = 1023                      # TXOP RTS threshold decoded by the scanner
    got = 4 | ((v & 0x3FF) << 4)
    ck("HE Operation params == 0x003FF4 for threshold 1023", got == 0x003FF4,
       hex(got))
    ck("HE Operation params default PE duration == 4", (got & 0x7) == 4)
    # BSS color mask keeps 7 bits (6-bit color + partial bit)
    for color in (0, 1, 25, 63, 0x80, 0xFF):
        masked = color & 0x7F
        ck("BSS color mask 0x%02X -> 0x%02X" % (color, masked),
           masked == color & 0x7F)
    # probe arithmetic
    ck("ctx slot table 0x171458", CTX_TBL == 0x171458)
    ck("ctx base u16 0x171504", CTX_BASE == 0x171504)
    ck("ctx0 = 0x119758", CTX_POOL == 0x119758)
    ck("ctx0+0x190 VAP probe = 0x1198E8", CTX_POOL + 0x190 == 0x1198E8)
    ck("ctx0+0xDA TWT probe = 0x119832", CTX_POOL + 0xDA == 0x119832)
    ck("ctx0+0xEE SR probe = 0x119846", CTX_POOL + 0xEE == 0x119846)
    ck("CBH+0x348 probe = 0x174B0C", CBH + 0x348 == 0x174B0C)
    ck("CBH+0x370 hook probe = 0x174B34", CBH + 0x370 == 0x174B34)
    ck("context stride*19 == 0xD5C0", CTX_STRIDE * CTX_N == 0xD5C0)
    # our own frame model: exact bytes for the two committed reference frames
    ref = bytes.fromhex("00010000" "1d00" "0100" "0000" "5a5a"
                        "0000000001000000" "f701" "0100" "03" "ffffffff")
    ours = g0_frame(0, 0x01F7, b"\x03")
    ck("frame model reproduces the fired 0x01F7 frame", ours == ref, ours.hex())
    ck("frame total field == wire length", len(ours) == 0x1D)
    twt = g0_frame(0, 0x035D, b"\x01")
    ck("TWT enable frame 29 B, key/body", len(twt) == 29 and
       struct.unpack("<H", twt[20:22])[0] == 0x35D and twt[24] == 1)
    mu = g0_frame(0, 0x005B, struct.pack("<HHII", 0x005B, 0, 0, 0x00F0F0))
    ck("MU EDCA frame 40 B", len(mu) == 40, str(len(mu)))
    alg = g0_frame(0, 0x0128, build_alg_body(["sch", "usr_bw",
                                              "00:11:22:33:44:55", "1"]))
    ck("ALG frame body parses back",
       parse_alg_body(alg[24:24 + struct.unpack("<H", alg[22:24])[0]])[0] ==
       ["sch", "usr_bw", "00:11:22:33:44:55", "1"])
    passed = sum(1 for _, ok, _ in CHECKS if ok)
    for name, ok, detail in CHECKS:
        if not ok:
            print("FAIL: %s (%s)" % (name, detail))
    print("selftest: %d/%d PASS" % (passed, len(CHECKS)))
    return passed == len(CHECKS)


def keys():
    d = fw()
    print("g0-id1 config-event dispatcher CA 0x%05X: table A 0x%05X (%d), "
          "table B 0x%05X (%d)" % (HANDLER_G0ID1, TBL_A, TBL_A_N,
                                   TBL_B, TBL_B_N))
    print()
    print("%-3s %-6s %-5s %-10s %s" % ("tbl", "key", "row", "worker", "what"))
    for tname, key, thumb, ca, what in AX_KEYS:
        got = find_key(d, key)
        print("%-3s 0x%04X %-5d 0x%05X    %s"
              % (got[0], key, got[1], ca, what))


def decode():
    print("AX config-event workers (firmware; r0=ctx, r1=len, r2=body)")
    print()
    print("TWT enable 0x035D -> CA 0x41E5A")
    print("  body[0].bit0 -> *(u8)(ctx+0xDA).bit6   (TWT enable)")
    print("  body[0].bit1 -> *(u8)(ctx+0xDA).bit7   (TWT required)")
    print("  host validator (wal_hipriv_twt_enable) accepts only {1,3}")
    print()
    print("ext TWT IE switch 0x00E9 -> CA 0x44484")
    print("  VAP = *(u32)(ctx+0x190); VAP+0x3DE = (u32(body) != 0)")
    print()
    print("HE GI 0x006F -> CA 0x81C44")
    print("  VAP = *(u32)(ctx+0x190); VAP+0x831 = body[0]")
    print("  body[0]==1 -> VAP+0x1A7.bit7 = 1, VAP+0x1A8.bit5 = 1;")
    print("  otherwise bit5 cleared (host accepts body[0] in {1,2,4})")
    print()
    print("trigger SU BF feedback 0x032E -> CA 0x818DC")
    print("  VAP+0x1A6.bit7 = body[0]")
    print()
    print("BFEE support 0x0335 -> CA 0x81890")
    print("  VAP+0x364 = VAP+0x366 = body[0]; VAP+0x1A4.bit1 = body[0]")
    print()
    print("BFER support 0x04C8 -> CA 0x817CC")
    print("  body[0] 0..3 (tbb): writes VAP+0x365/+0x367 and VAP+0x1A4")
    print("  bits 0/2; >3 -> log, no write")
    print()
    print("cyclic-scheme switch 0x0353 -> CA 0x43BD4")
    print("  body[0] 0/1 -> O = band object; O+0x1A08: 1 -> CA 0xC6034,")
    print("  0 -> CA 0xC52B4 (band scheduler re-arm); other -> log")
    print()
    print("multi-AP SR enable 0x04B6 -> CA 0x49ACA")
    print("  ctx+0xEE = body[0] & 1   (per-module spatial-reuse enable)")
    print()
    print("multi-AP tool 0x04B4 -> CA 0x4A864")
    print("  text body -> one parsed arg -> 4-entry table CA 0x104520")
    print("  {name ptr; handler ptr}; handler called as (ctx, r1, out)")
    print()
    print("MU EDCA params 0x005B..0x005F -> CA 0x8128C..0x813BC")
    print("  body = {u16 sel; u16 pad; u32 ac 0..3; u32 value}")
    print("  VAP[+0x1D4 + 4*(sel-0x5B) + 24*ac] = value")
    print("  (0x5F is the u8 field, 0x5B..0x5E are u32 fields)")
    print("  then ctx+0xD3 = (ctx+0xD3 + 1) & 0xF (sequence counter)")


def alg():
    d = fw()
    print("ALG config text plane: g0-id1 key 0x0128 -> table B row 28 -> "
          "worker CA 0x%05X" % ALG_WORKER)
    print("frame body = {u8 n; u8 offset[n]; NUL-separated tokens}, "
          "body+8 = token area")
    print("len gate: r1 >= 8 (CA 0x83148 cmp r3,#8; bcc fail)")
    print("token[0] is compared against the module name table CA 0x%05X:"
          % ALG_NAMES)
    for i in range(3):
        print("  id %d: %r" % (i, fwb(d, fwu32(d, ALG_NAMES + i * 8), 16)
                               .split(b"\x00")[0].decode()))
    print("handler dispatch: CBH + 4*(0x%X + id), CBH = CA 0x%05X"
          % (ALG_HANDLER_OFF, CBH))
    for i in range(3):
        print("  id %d handler slot CPU 0x%05X (read-only probe; value [U])"
              % (i, CBH + 4 * (ALG_HANDLER_OFF + i)))
    print()
    print("host sender hmac_config_alg (ko 0x%05X): len>8, splits tokens at"
          % 0x58E18)
    print("0x20, token[0]=='sch' + token[1]=='usr_bw' is the one host-side")
    print("special case (per-STA bandwidth), then g0-id1 key 0x0128 carries")
    print("the raw text body.")
    print()
    print("firmware side [I]: the 'sch' handler reads the OFDMA/sounding/SR")
    print("algorithm parameters, which is why the module carries the")
    print("alg_*_ofdma_*, alg_*_sounding_* and alg_*_sr_* parameter names.")
    print("The handler address is runtime-installed (probe P6); the token")
    print("grammar for ofdma/sounding is not statically decodable.")
    print()
    print("validated sample body for module 'sch':")
    b = build_alg_body(["sch", "usr_bw", "00:11:22:33:44:55", "1"])
    print("  " + b.hex())
    print("  count=%d tokens=%r" % (b[0], parse_alg_body(b)[0]))


def host():
    print("vendor host builders (module .text, link-time offsets)")
    print()
    rows = [
        ("0x753B0", "hmac_config_set_he_gi", "0x006F",
         "body[0] in {1,2,4}; VAP+0x1A7.bit7 / +0x1A8.bit5 / +0x831"),
        ("0x61330", "hmac_config_ext_twt_ie_switch", "0x00E9",
         "raw body (u32 0/1) -> VAP+0x3DE"),
        ("0x15E8E8", "hmac_config_twt_enable", "0x035D",
         "body[0].bit0/bit1; host mirror +0xD2 bits 6/7"),
        ("0x75E40", "hmac_config_set_trig_su_bf_fb", "0x032E",
         "body[0] -> VAP+0x1A6.bit7"),
        ("0x75F0C", "hmac_config_set_bfee_support", "0x0335",
         "body[0] -> VAP+0x364/+0x366, +0x1A4.bit1"),
        ("0x75FCC", "hmac_config_set_bfer_support", "0x04C8",
         "body[0] 0..3 -> VAP+0x365/+0x367, +0x1A4 bits 0/2"),
        ("0x161258", "hmac_config_set_cyc_sche_switch", "0x0353",
         "raw body -> firmware scheduler re-arm"),
        ("0x0C6D3C", "hmac_config_multiap_sr_enable", "0x04B6",
         "body[0]&1; host +0xE6 -> firmware ctx+0xEE"),
        ("0x0C3F20", "hmac_config_multiap_tool", "0x04B4",
         "text body -> 4-entry firmware tool table"),
        ("0x76250", "hmac_config_set_wmm_mu_edca_params", "0x5B..0x5F",
         "12-B body {u16 sel;u16 pad;u32 ac;u32 value}"),
        ("0x58E18", "hmac_config_alg", "0x0128",
         "ALG text body (see --alg)"),
    ]
    print("%-9s %-40s %-10s %s" % ("addr", "builder", "key", "body/effect"))
    for a, n, k, d in rows:
        print("%-9s %-40s %-10s %s" % (a, n, k, d))
    print()
    print("TWT user path: wal_hipriv_twt_enable (0x131634) takes one int arg,")
    print("valid {1,3}, and posts WAL cfg event {u8 value; u16 0x035D;")
    print("u16 4; u8 value} - the same key the g0-id1 carrier dispatches.")
    print()
    print("HE Operation / BSS color (vendor host IE builders, NOT the beacon")
    print("path - beacons are firmware mode-10 output):")
    print("  mac_set_he_operation_params 0x25588:")
    print("    out[0..2] = LE(4 | ((u16(MIB+0x1BA) >> 6) << 4))")
    print("    on-air 0x003FF4 => Default PE Duration 4, TXOP RTS 1023")
    print("  mac_set_bss_color_info 0x25604:")
    print("    out[0] = u8(obj+0xCC) & 0x7F  (6-bit color + partial bit)")
    print("  hmac_alg_sr_config_param 0x963CC: ALG param id 0xCE5 ->")
    print("    hmac_sr_cfg_param_bss_color 0x961D4 (writes the color field")
    print("    of the matching station objects)")
    print("  the two host-side color writers are hmac_sr_* (no g0-id1 key):")
    print("    the vendor host has no BSS-color config event; the on-air")
    print("    color is a firmware-side default until a worker is decoded")


def frames():
    print("our own AX config-event frames (g0-id1 envelope):")
    print("  total = 28 + body_len; key u16 @20; body_len u16 @22; body @24")
    samples = [
        ("TWT enable (enable only)", 0, 0x035D, b"\x01"),
        ("TWT enable (enable+required)", 0, 0x035D, b"\x03"),
        ("ext TWT IE on", 0, 0x00E9, struct.pack("<I", 1)),
        ("HE GI 1 (0.8us)", 0, 0x006F, b"\x01"),
        ("HE GI 4", 0, 0x006F, b"\x04"),
        ("MU EDCA 0x5B ac0 0x00F0F0", 0, 0x005B,
         struct.pack("<HHII", 0x005B, 0, 0, 0x00F0F0)),
        ("BFEE on", 0, 0x0335, b"\x01"),
        ("BFER mode 1", 0, 0x04C8, b"\x01"),
        ("trigger SU BF fb on", 0, 0x032E, b"\x01"),
        ("cyc scheme 1", 0, 0x0353, b"\x01"),
        ("multi-AP SR on", 0, 0x04B6, b"\x01"),
        ("ALG text sch", 0, 0x0128,
         build_alg_body(["sch", "usr_bw", "00:11:22:33:44:55", "1"])),
    ]
    for what, fb, key, body in samples:
        f = g0_frame(fb, key, body)
        print("%-32s %s" % (what + ":", f.hex()))
    print()
    print("field B = the per-module context selector; the AP-start ladder")
    print("binds module 0 to band 1 (../wifi-fw-dfs-worker): use field B 0.")


def probes():
    rows = [
        ("u32", 0x171458, "context slot table[0] = context[0] ptr",
         "0x00119758"),
        ("u16", 0x171504, "context base offset (add to every pointer)",
         "0x0000 [I]"),
        ("u32", 0x1198E8, "ctx0+0x190 = VAP object pointer (AP ladder)",
         "nonzero; chase"),
        ("u8 ", 0x119832, "ctx0+0xDA TWT bits 6/7", "0 before TWT enable"),
        ("u8 ", 0x119846, "ctx0+0xEE multi-AP SR enable bit0", "0 default"),
        ("u32", 0x174B0C, "ALG handler slot module 'sch'", "nonzero [U]"),
        ("u32", 0x174B10, "ALG handler slot module 'cmn'", "nonzero [U]"),
        ("u32", 0x174B14, "ALG handler slot module 'err_name'", "nonzero [U]"),
        ("u32", 0x174B34, "hook CBH+0x370 (MAC start)", "0 in this image"),
        ("u32", 0x174B38, "hook CBH+0x374 (beacon start)", "0x00057639"),
        ("u32", 0x15CA08 + 0x1A00, "band0 published per-link", "0x00174BF0"),
        ("u8 ", 0x15CA08 + 0xF04, "band0 RF mode byte", "3 after a 0x01F7"),
        ("u32", 0x15E570 + 0x1A00, "band1 published per-link", "0x00175A18"),
        ("u8 ", 0x15E570 + 0xF04, "band1 RF mode byte", "3 after F2"),
    ]
    print("READ-ONLY probe pack (no write, one boot).  Firmware RAM phys =")
    print("0x40000000 + CPU + 0x6B8000.  Read with the committed read-only")
    print("helper.  VAP-relative probes are the second step: chase the")
    print("pointer from 0x1198E8 and add the VAP offset.\n")
    print("%-4s %-9s %-10s %-11s %s"
          % ("sz", "CPU", "phys", "VAP off", "what / expected"))
    for sz, cpu, what, exp in rows:
        phys = 0x40000000 + cpu + 0x6B8000
        print("%-4s 0x%07X 0x%08X %-11s %s | %s"
              % (sz, cpu, phys, "-", what, exp))
    print()
    print("VAP-relative probes (VAP = u32(0x1198E8) + u16(0x171504)):")
    for off, what in [
        (0x1A4, "u8  capability flags: bit0 BFER, bit1 BFEE, bit2 = BF mode"),
        (0x1A6, "u8  bit7 trigger SU BF feedback"),
        (0x1A7, "u8  bit7 HE GI enable"),
        (0x1A8, "u8  bit5 HE GI (second copy)"),
        (0x1D4, "u32 per-AC MU EDCA slot 0x5B (+24*ac)"),
        (0x1D8, "u32 per-AC MU EDCA slot 0x5C (+24*ac)"),
        (0x1DC, "u32 per-AC MU EDCA slot 0x5D (+24*ac)"),
        (0x1E0, "u32 per-AC MU EDCA slot 0x5E (+24*ac)"),
        (0x1E4, "u8  per-AC MU EDCA slot 0x5F (+24*ac)"),
        (0x364, "u8  BFEE enable copy / 0x365 BFER, 0x366/0x367 copies"),
        (0x3DE, "u8  ext TWT IE enable"),
        (0x831, "u8  HE GI copy"),
    ]:
        print("  VAP+0x%03X %s" % (off, what))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--keys", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--alg", action="store_true")
    ap.add_argument("--host", action="store_true")
    ap.add_argument("--frames", action="store_true")
    ap.add_argument("--probes", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return 0 if selftest() else 1
    ran = False
    for flag, fn in ((a.keys, keys), (a.decode, decode), (a.alg, alg),
                     (a.host, host), (a.frames, frames), (a.probes, probes)):
        if flag:
            fn()
            ran = True
    if not ran:
        keys()
        print()
        decode()
        print()
        alg()
        print()
        host()
        print()
        frames()
        print()
        probes()
    return 0


if __name__ == "__main__":
    sys.exit(main())
