#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
dp_txrx_static_decode.py - host-only static-decode assertions for the
wifi-dp-txrx lane (shared task task-122).

Re-derives, from the LOCAL vendor artifacts (never committed), every fact the
lane's portable library and kernel extension encode:

  * vendor host module hi5622v100_wifi.ko (2.4.15, md5-pinned):
      shuangta_tx_msdu_ring_buf_alloc   table entry {devva,0,0} + slot fields
      hmac_tx_msdu_ring_push            buf devva transform, ctl length/vap bits,
                                        published wptr -> table entry dword1
      hmac_tx_msdu_ring_proc            device rptr read from table dword2,
                                        processed-entry magic 0xE0
      hmac_tx_msdu_dscr_add             20-byte below-data descriptor, zero hdr
      shuangta_get_msdu_info_ring_addr  ring buf devva = skb->data - 0x10
      shuangta_tx_msdu_info_ring_magic_set  ctl byte7 |= 0xE0
      hmac_tx_frame_control_dscr_set    ctl byte2 bits 4/5 = vap type 1/2/3
      shuangta_host_rx_get_msdu_info_dscr / shuangta_rx_host_init_dscr_queue
                                        RX descriptor offsets + ring geometry
  * vendor firmware FIRMWARE.bin (pinned sha256/len): identity pin only this
    lane adds no firmware decode dependency (the TX publish path is host|table
    side; the previous lane already pinned the 0x320 worker).

No vendor byte, object code or firmware content is written to the repository:
only booleans/counts and derived constants are printed. Missing local
artifacts -> SKIP (exit 0) so the lane build works without the teardown;
--require turns a missing artifact into a failure.
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
LANE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

passed = 0
failed = 0
skipped = 0
required = "--require" in sys.argv


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print("FAIL: %s%s" % (name, (" - " + detail) if detail else ""))


def skip(name, why):
    global skipped
    if required:
        check(name, False, why)
    else:
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
        self._cache = {}

    def sym(self, name):
        for s in self.symtab.iter_symbols():
            if s.name == name:
                return s
        return None

    def insns(self, name):
        if name in self._cache:
            return self._cache[name]
        import capstone
        s = self.sym(name)
        if s is None or s["st_size"] == 0:
            return []
        sec = self.elf.get_section(s["st_shndx"])
        data = sec.data()[s["st_value"]:s["st_value"] + s["st_size"]]
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        out = [(i.mnemonic, i.op_str) for i in md.disasm(data, s["st_value"])]
        self._cache[name] = out
        return out

    def calls(self, name):
        s = self.sym(name)
        if s is None or s["st_size"] == 0:
            return []
        sec = self.elf.get_section(s["st_shndx"])
        data = sec.data()[s["st_value"]:s["st_value"] + s["st_size"]]
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        out = []
        for i in md.disasm(data, s["st_value"]):
            target = self.rels.get((sec.name, i.address))
            if target:
                out.append(target)
        return out

    def op_has(self, name, needles):
        for mnem, op in self.insns(name):
            text = mnem + " " + op
            if all(n in text for n in needles):
                return True
        return False


def check_ko(path):
    import capstone  # noqa: F401  (fail early with a clear traceback)
    print("== vendor host module: %s" % path)
    check("vendor module md5 pin", md5_file(path) == VENDOR_KO_MD5)
    ko = Ko(path)

    syms = {
        "shuangta_tx_msdu_ring_buf_alloc": 0x38f5c,
        "hmac_tx_msdu_ring_push": 0x18f18,
        "hmac_tx_msdu_ring_proc": 0x1cab4,
        "hmac_tx_msdu_ring_rw_index_check": 0x18cc0,
        "hmac_tx_msdu_ring_init": 0x18d7c,
        "hmac_tx_msdu_dscr_init": 0x18654,
        "hmac_tx_msdu_dscr_add": 0x19b40,
        "hmac_tx_frame_control_dscr_set": 0x18b9c,
        "shuangta_get_msdu_info_ring_addr": 0x38ad8,
        "shuangta_set_tx_msdu_info_ring": 0x389dc,
        "shuangta_tx_msdu_info_ring_magic_set": 0x389fc,
        "shuangta_tx_msdu_info_ring_magic_check": 0x38a40,
        "hmac_get_user_tid_tx_msdu_info_ring_table_addr": 0x183a0,
        "shuangta_rx_host_init_dscr_queue": 0x37888,
        "shuangta_host_rx_get_msdu_info_dscr": 0x3843c,
        "hmac_rx_ring_entry_proc": 0x101e8,
    }
    for name, val in syms.items():
        s = ko.sym(name)
        check("symbol %s @0x%x" % (name, val),
              s is not None and s["st_value"] == val)

    # --- TX ring buffer / table entry ------------------------------------
    alloc = "shuangta_tx_msdu_ring_buf_alloc"
    check("ring_alloc: noncached alloc", "oal_noncache_alloc" in ko.calls(alloc))
    check("ring_alloc: hostca->devva conversion",
          "pcie_if_hostca_to_devva" in ko.calls(alloc))
    for off in (0x40, 0x44, 0x48, 0x4c, 0x50, 0x54):
        check("ring_alloc: slot field +0x%x" % off,
              ko.op_has(alloc, ["#0x%x]" % off]))
    check("ring_alloc: publish barriers dmb+dsb+sev",
          ko.op_has(alloc, ["dmb"]) if False else
          any(m == "dmb" for m, _ in ko.insns(alloc)) and
          any(m == "dsb" for m, _ in ko.insns(alloc)) and
          any(m == "sev" for m, _ in ko.insns(alloc)))

    # --- TX frame buffer address transform --------------------------------
    gaddr = "shuangta_get_msdu_info_ring_addr"
    check("ring buf devva = skb->data - 0x10",
          ko.op_has(gaddr, ["r3, r3, #0x10"]) and
          ko.op_has(gaddr, ["str", "[r2]"]))
    push = "hmac_tx_msdu_ring_push"
    check("push: +0xC0600000 host-VA transform",
          ko.op_has(push, ["r1, r1, #0x600000"]) and
          (ko.op_has(push, ["r1, r1, #-1073741824"]) or
           ko.op_has(push, ["r1, r1, #0xc0000000"])))
    check("push: writes ring entry", "shuangta_set_tx_msdu_info_ring" in
          ko.calls(push))
    check("push: MSDU-table wptr publish (post module +4)",
          ko.op_has(push, ["#0xb20]"]) and
          ko.op_has(push, ["str", "[r3, #4]"]))
    check("push: published-index copies (host ring slot)",
          ko.op_has(push, ["ldr", "#0xad4]"]) and
          ko.op_has(push, ["str", "#0xad4]"]))
    check("push: dmb before publish", any(m == "dmb" for m, _ in ko.insns(push)))
    check("push: frame length from skb->len into ctl[11:0]",
          ko.op_has(push, ["#0xc8]"]) and ko.op_has(push, ["bfi"]))
    magic_set = "shuangta_tx_msdu_info_ring_magic_set"
    check("processed-entry magic = ctl byte7 |= 0xE0",
          ko.op_has(magic_set, ["orr", "#0xe0"]))
    check("magic markers checked in ring_proc",
          "shuangta_tx_msdu_info_ring_magic_check" in
          ko.calls("hmac_tx_msdu_ring_proc"))
    check("ring_proc: device rptr from table dword2 (+8)",
          ko.op_has("hmac_tx_msdu_ring_proc", ["ldr", "#8]"]) and
          ko.op_has("hmac_tx_msdu_ring_proc", ["ldr", "#4]"]))
    check("ring_proc: consumes entries via magic_set after get",
          "shuangta_get_tx_msdu_info_ring" in
          ko.calls("hmac_tx_msdu_ring_proc") and
          "shuangta_tx_msdu_info_ring_magic_set" in
          ko.calls("hmac_tx_msdu_ring_proc"))
    check("ring_proc: packed-index sanity via rw_index_check",
          "hmac_tx_msdu_ring_rw_index_check" in
          ko.calls("hmac_tx_msdu_ring_proc"))
    check("ring_depth 0x40 in dscr_init",
          ko.op_has("hmac_tx_msdu_dscr_init", ["#0x40"]))

    # --- TX per-frame descriptor -----------------------------------------
    dscr = "hmac_tx_msdu_dscr_add"
    check("dscr_add: 0x14 headroom expand", "pskb_expand_head" in ko.calls(dscr))
    check("dscr_add: 20-byte descriptor below skb->data",
          ko.op_has(dscr, ["#0x14"]) and
          (ko.op_has(dscr, ["str", "[r8, #-0x14]"]) or
           ko.op_has(dscr, ["str", "[r8, #-20]"])))
    check("dscr_add: zeroes the 0x14 bytes (memset_s)",
          "memset_s" in ko.calls(dscr))
    check("dscr_add: skb->len + 0x14 DMA map",
          "oal_dma_map_single" in ko.calls(dscr))
    fcs = "hmac_tx_frame_control_dscr_set"
    check("frame_control: ctl byte2 masks 0x30", ko.op_has(fcs, ["bic", "#0x30"]))
    check("frame_control: vap type values 0x10/0x20/0x30",
          ko.op_has(fcs, ["#0x10"]) and ko.op_has(fcs, ["#0x20"]) and
          ko.op_has(fcs, ["#0x30"]))
    check("frame_control: writes ctl byte 2 (+6)", ko.op_has(fcs, ["[r2, #6]"]))

    # --- RX rings / descriptor -------------------------------------------
    init = "shuangta_rx_host_init_dscr_queue"
    for imm in (0x400, 0x800, 0x3ffc, 0x63c, 0xb0, 0x100, 0x200, 0xfff):
        check("rx_init: ring/buffer constant #0x%x" % imm,
              ko.op_has(init, ["#0x%x" % imm]))
    rxd = "shuangta_host_rx_get_msdu_info_dscr"
    for imm in (0x8, 0xc, 0xe, 0x10, 0x14, 0x16, 0x18, 0x19, 0x1b, 0x1c,
                0x1e, 0x20, 0x21, 0x23, 0x28):
        check("rx desc getter: offset field #0x%x" % imm,
              ko.op_has(rxd, ["#0x%x" % imm]))


def check_fw(path):
    print("== vendor firmware image: %s" % path)
    check("firmware sha256 pin", sha256_file(path) == FW_SHA256)
    check("firmware length pin", os.path.getsize(path) == FW_LEN)


def check_sources():
    print("== lane source consistency (no device) ==")
    hdr = open(os.path.join(LANE, "src", "dp_txrx.h")).read()
    c = open(os.path.join(LANE, "src", "dp_txrx.c")).read()
    k = open(os.path.join(LANE, "src", "hi5622_dp_txrx.c")).read()
    check("header: 12-byte table entry", "TABLE_ENTRY_BYTES 12u" in hdr)
    check("header: table wptr at +4 / rptr at +8 (documented)",
          "published_wptr" in hdr and "device_consumed_rptr" in hdr)
    check("header: ACP alias delta 0x01000000", "0x01000000u" in hdr)
    check("header: processed magic mask 0xE0000000",
          "0xE0000000u" in hdr)
    check("header: frame buffer MPDU offset 20",
          "(DP_TXRX_FRAME_HOST_HDR + DP_TXRX_FRAME_DMA_HDR)" in hdr and
          "DP_TXRX_FRAME_AREA 0x400u" in hdr)
    check("header: RX descriptor length 0x28", "RX_DESC_LEN 0x28u" in hdr)
    check("header: RX rings 256/512/4095",
          "RX_NORM_DEPTH 256u" in hdr and "RX_SMALL_DEPTH 512u" in hdr and
          "RX_CMP_DEPTH 4095u" in hdr)
    check("header: MAC cmp wptr/rptr offsets 0x4c/0x40",
          "RXCMP_WPTR 0x4cu" in hdr and "RXCMP_RPTR 0x40u" in hdr)
    check("source: publish is entry dword1 (wptr)", "table_entry_pack" in c)
    check("source: probe request builder", "build_probe_req" in c)
    check("source: null/qos-null/arp builders",
          "build_null_data" in c and "build_qos_null" in c and
          "build_arp_bcast" in c)
    check("source: capture-only RX plan never moves pointers",
          "capture-only" in hdr)
    check("kernel: write gate requires 0x320 txq sync",
          "h2d_txq_addr_sent" in k)
    check("kernel: forbidden CA documented, no mailbox id-5 path",
          "CA 0x400392f0" in k and "mailbox id 5" not in k)
    check("kernel: RX poll is read-only (no dp_mac_write call)",
          "dp_mac_write" not in k and "writel" in k)
    check("patch: only hi5622_dp.c + Kbuild touched",
          open(os.path.join(LANE, "patches",
                            "0001-hi5622-dp-txrx.patch")).read().count(
              "--- a/src/") == 2)


def main():
    if not os.path.isfile(VENDOR_KO_DEFAULT):
        skip("vendor host module", "not present")
    else:
        check_ko(VENDOR_KO_DEFAULT)
    if not os.path.isfile(FW_DEFAULT):
        skip("vendor firmware image", "not present")
    else:
        check_fw(FW_DEFAULT)
    check_sources()
    print("SUMMARY: %d passed, %d failed, %d skipped" %
          (passed, failed, skipped))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
