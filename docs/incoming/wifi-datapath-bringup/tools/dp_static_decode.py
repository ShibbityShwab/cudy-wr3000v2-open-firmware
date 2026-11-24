#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
dp_static_decode.py - host-only static-decode assertions for the
wifi-datapath-bringup lane (shared task task-111).

Re-derives, from the LOCAL vendor artifacts (never committed), the constants
and code paths the portable library and the kernel extension encode:

  * vendor host module (hi5622v100_wifi.ko, 2.4.15) - pinned by md5:
      hmac_txq_table_init             : 0x2524-byte host TXQ table allocation,
                                        MSDU-table-base init, then address sync
      hmac_txq_table_addr_sync        : hdpp_config_send_event(cmd=0x320, len=4,
                                        data = [dev+0x948] device VA)
      hmac_tx_msdu_info_ring_table_addr_init : wlan_get_d2h_read_msg ->
                                        memmap_get_dev_acp_addr -> MAC +0x10
      hmac_get_user_tid_tx_msdu_info_ring_table_addr : table base + 12*(tid+user*8)
      shuangta_tx_msdu_ring_buf_alloc : ring alloc + {devVA,0,0} table entry
      hmac_tx_msdu_dscr_init          : per-TID ring-slot depth default 0x40
      shuangta_rx_host_init_dscr_queue: RX ring alloc sizes 0x400 / 0x800 / 0x3ffc,
                                        buffer lengths 0x63c / 0xb0, sizes 256/512/4095
      shuangta_ba_info_res_alloc      : tx_ba_info 0x800 B, depth 0x100
  * vendor firmware image (FIRMWARE.bin, pinned sha256/len):
      group-0 id-1 worker CA 0x81445 (key 0x0320): stores the 4-byte payload
      into firmware RAM at (band array) + 0x252c*band + 0x2524
      group-0 id-1 worker CA 0x81039 (key 0x00d8): data[0] -> ctx+0x190+0x4c

No vendor byte, object code, firmware image content or per-unit value is
written to the repository: only booleans/counts/derived constants are printed.
Missing local artifacts -> SKIP (exit 0) so the lane build works on hosts
without the vendor teardown; --require turns missing artifacts into a failure.
"""
import hashlib
import os
import sys

VENDOR_KO_DEFAULT = ("/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/"
                     "rootfs-2.4.15/lib/hisilicon/ko/hi5622v100_wifi.ko")
VENDOR_KO_MD5 = "1745ef77e728c411cef1921af3dc8731"
FW_DEFAULT = "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin"
FW_SHA256 = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
FW_LEN = 928920

passed = 0
failed = 0
skipped = 0


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print("PASS: %s" % name)
    else:
        failed += 1
        print("FAIL: %s%s" % (name, (" - " + detail) if detail else ""))


def skip(name, why):
    global skipped
    skipped += 1
    print("SKIP: %s (%s)" % (name, why))


def md5_file(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class Ko:
    def __init__(self, path):
        from elftools.elf.elffile import ELFFile
        self.elf = ELFFile(open(path, "rb"))
        self.symtab = self.elf.get_section_by_name(".symtab")
        self.rels = {}
        for sec in self.elf.iter_sections():
            if sec.header.sh_type not in ("SHT_REL", "SHT_RELA"):
                continue
            tgt = sec.header.sh_info
            tname = self.elf.get_section(tgt).name if tgt else None
            for rel in sec.iter_relocations():
                sym = self.symtab.get_symbol(rel.entry.r_info_sym)
                if sym.name:
                    self.rels[(tname, rel.entry.r_offset)] = sym.name

    def sym(self, name):
        for s in self.symtab.iter_symbols():
            if s.name == name:
                return s
        return None

    def insns(self, name):
        """Disassemble a .text function (ARM state, vendor kernel module)."""
        import capstone
        s = self.sym(name)
        if s is None or s["st_size"] == 0:
            return []
        sec = self.elf.get_section(s["st_shndx"])
        data = sec.data()[s["st_value"]:s["st_value"] + s["st_size"]]
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        return [(i.mnemonic, i.op_str) for i in md.disasm(data, s["st_value"])]

    def calls(self, name):
        import capstone
        s = self.sym(name)
        if s is None or s["st_size"] == 0:
            return []
        sec = self.elf.get_section(s["st_shndx"])
        tname = sec.name
        data = sec.data()[s["st_value"]:s["st_value"] + s["st_size"]]
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        out = []
        for i in md.disasm(data, s["st_value"]):
            target = self.rels.get((tname, i.address))
            if target:
                out.append(target)
        return out

    def has_imm(self, name, imm):
        lit = "0x%x" % imm
        return any(lit in op for _m, op in self.insns(name))


def check_ko(path):
    import capstone  # noqa: F401  (fail early with a clear traceback)
    print("== vendor host module: %s" % path)
    check("vendor module md5 pin", md5_file(path) == VENDOR_KO_MD5)
    ko = Ko(path)
    syms = {
        "hmac_txq_table_init": (0x1b0b4, 0x1e0),
        "hmac_txq_table_addr_sync": (0x1af00, 0x10c),
        "hmac_tx_msdu_info_ring_table_addr_init": (0x184f0, 0x164),
        "hmac_get_user_tid_tx_msdu_info_ring_table_addr": (0x183a0, 0x150),
        "shuangta_tx_msdu_ring_buf_alloc": (0x38f5c, 0x128),
        "hmac_tx_msdu_dscr_init": (0x18654, 0xc8),
        "shuangta_rx_host_init_dscr_queue": (0x37888, 0x460),
        "shuangta_ba_info_res_alloc": (0x37d18, 0x210),
        "hmac_update_user_tid_txq_table": (0x1b294, 0x238),
        "hdpp_config_send_event": (0x59c0, 0x18c),
    }
    for name, (val, size) in syms.items():
        s = ko.sym(name)
        check("symbol %s @0x%x/0x%x" % (name, val, size),
              s is not None and s["st_value"] == val and s["st_size"] == size)

    # hmac_txq_table_init: 0x2524 alloc + the MSDU-table-init then addr-sync order
    calls = ko.calls("hmac_txq_table_init")
    check("txq table alloc size 0x2524 in hmac_txq_table_init",
          ko.has_imm("hmac_txq_table_init", 0x2524))
    check("txq init calls MSDU-table-base init before addr sync",
          "hmac_tx_msdu_info_ring_table_addr_init" in calls and
          "hmac_txq_table_addr_sync" in calls and
          calls.index("hmac_tx_msdu_info_ring_table_addr_init") <
          calls.index("hmac_txq_table_addr_sync"))
    # hmac_txq_table_addr_sync: cmd 0x320, len 4, data = [dev+0x948]
    ins = ko.insns("hmac_txq_table_addr_sync")
    check("addr sync sends hdpp_config_send_event",
          "hdpp_config_send_event" in ko.calls("hmac_txq_table_addr_sync"))
    check("addr sync cmd 0x320 / len 4",
          any(m in ("mov", "movw") and op_str == "r1, #0x320"
              for m, op_str in ins) and
          any(m in ("mov", "movw") and op_str == "r2, #4"
              for m, op_str in ins))
    check("addr sync payload = [dev+0x948] (TXQ table DMA address)",
          any(m == "ldr" and "#0x948]" in op_str for m, op_str in ins))
    # MSDU-table-base init chain
    calls = ko.calls("hmac_tx_msdu_info_ring_table_addr_init")
    check("MSDU table base init: d2h read msg -> ACP -> MAC +0x10",
          "wlan_get_d2h_read_msg" in calls and
          "memmap_get_dev_acp_addr" in calls and
          "shuangta_set_msdu_info_ring_ptr_table_base" in calls)
    # table entry (user,tid): base + 12*(tid + user*8)
    ins = ko.insns("hmac_get_user_tid_tx_msdu_info_ring_table_addr")
    check("MSDU table entry stride 12 and user*8 index",
          any(m == "mov" and op_str == "r1, #0xc" for m, op_str in ins) and
          any(m == "add" and "lsl #3" in op_str for m, op_str in ins) and
          any(m == "ldr" and "#0x92c]" in op_str for m, op_str in ins))
    calls = ko.calls("hmac_get_user_tid_tx_msdu_info_ring_table_addr")
    check("MSDU table entry resolved through devca->hostva",
          "oal_pcie_devca_to_hostva" in calls)
    # ring buffer alloc writes {devva,0,0} into the table entry
    ins = ko.insns("shuangta_tx_msdu_ring_buf_alloc")
    check("TX ring alloc: noncached alloc + 0x40 entry writes",
          "oal_noncache_alloc" in ko.calls("shuangta_tx_msdu_ring_buf_alloc") and
          sum(1 for m, op_str in ins
              if m == "str" and (op_str.startswith("r0, [sb") or
                                 op_str.startswith("r8, [sb"))) >= 2)
    check("TX ring publish barriers dmb+dsb+sev",
          any(m == "dmb" for m, _ in ins) and any(m == "dsb" for m, _ in ins) and
          any(m == "sev" for m, _ in ins))
    # per-TID ring-slot depth default 0x40 (slot +0x38)
    ins = ko.insns("hmac_tx_msdu_dscr_init")
    check("TX MSDU ring depth default 0x40 (str r6,[r3,#-0x20], r6=0x40)",
          any(m == "mov" and op_str == "r6, #0x40" for m, op_str in ins) and
          any(m == "str" and "#-0x20]" in op_str for m, op_str in ins))
    # RX init sizes
    ins = ko.insns("shuangta_rx_host_init_dscr_queue")
    check("RX norm free ring 0x400 B (256 x 4)",
          ko.has_imm("shuangta_rx_host_init_dscr_queue", 0x400))
    check("RX small free ring 0x800 B (512 x 4)",
          ko.has_imm("shuangta_rx_host_init_dscr_queue", 0x800))
    check("RX completion ring 0x3ffc B (4095 x 4)",
          any(m == "movw" and op_str == "r0, #0x3ffc" for m, op_str in ins))
    check("RX buffer lengths 0x63c / 0xb0",
          ko.has_imm("shuangta_rx_host_init_dscr_queue", 0x63c) and
          any(m == "mov" and op_str == "r0, #0xb0" for m, op_str in ins))
    check("RX ring sizes 256/512/4095 written through 12-bit fields",
          any(m == "movw" and op_str == "r3, #0xfff" for m, op_str in ins) and
          ko.has_imm("shuangta_rx_host_init_dscr_queue", 0x100))
    # tx_ba_info: 0x800 B, depth 0x100
    ins = ko.insns("shuangta_ba_info_res_alloc")
    check("tx_ba_info alloc 0x800 B, depth 0x100",
          any(m == "mov" and op_str == "r0, #0x800" for m, op_str in ins) and
          any(m == "mov" and op_str == "fp, #0x100" for m, op_str in ins))
    # TXQ table user blocks: user*0x40 at +0x520/+4 (0x524 region), 8 TIDs
    ins = ko.insns("hmac_update_user_tid_txq_table")
    check("TXQ table user block stride 0x40 at +0x520/+4",
          any(m in ("add", "lsl") and "lsl #6" in op_str
              for m, op_str in ins) and
          ko.has_imm("hmac_update_user_tid_txq_table", 0x520))
    # hdpp_config_send_event writes key at hdr+0x14 / len at +0x16
    ins = ko.insns("hdpp_config_send_event")
    check("config event header key/len offsets",
          any(m == "strh" and "#0x14]" in op_str for m, op_str in ins) and
          any(m == "strh" and "#0x16]" in op_str for m, op_str in ins))


def check_fw(path):
    import capstone
    print("== vendor firmware image: %s" % path)
    data = open(path, "rb").read()
    check("firmware sha256/len pin",
          sha256_file(path) == FW_SHA256 and len(data) == FW_LEN)

    def insns(cpu, n):
        off = cpu - 0x40000
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
        return [(i.mnemonic, i.op_str)
                for i in md.disasm(data[off:off + n * 4], cpu)]

    worker = insns(0x81444, 16)
    check("fw worker 0x81445: band-array stride 0x252c",
          any(m == "movw" and op_str == "r4, #0x252c"
              for m, op_str in worker))
    check("fw worker 0x81445: TXQ table devVA stored at +0x2524",
          any(m == "movw" and op_str == "r1, #0x2524"
              for m, op_str in worker) and
          any(m == "str" and op_str.startswith("r3, [r2, r1]")
              for m, op_str in worker))
    worker = insns(0x81038, 12)
    check("fw worker 0x81039: key-0x00d8 data[0] -> ctx+0x190+0x4c",
          any(m == "ldrb" and op_str == "r1, [r2]" for m, op_str in worker) and
          any(m == "ldr.w" and op_str == "r3, [r0, #0x190]"
              for m, op_str in worker) and
          any(m == "strb.w" and op_str == "r1, [r3, #0x4c]"
              for m, op_str in worker))


def main(argv):
    require = "--require" in argv
    ko = os.environ.get("DP_VENDOR_KO", VENDOR_KO_DEFAULT)
    fw = os.environ.get("DP_FW", FW_DEFAULT)
    print("dp_static_decode: host-only assertions on the local vendor artifacts")
    got = False
    if os.path.isfile(ko):
        check_ko(ko)
        got = True
    elif require:
        check("vendor host module present", False, ko)
    else:
        skip("vendor host module", "not present: %s" % ko)
    if os.path.isfile(fw):
        check_fw(fw)
        got = True
    elif require:
        check("vendor firmware image present", False, fw)
    else:
        skip("vendor firmware image", "not present: %s" % fw)
    if not got:
        print("SUMMARY: SKIPPED (no local vendor artifact; nothing asserted)")
        return 0
    print("SUMMARY: %d passed, %d failed, %d skipped" %
          (passed, failed, skipped))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
