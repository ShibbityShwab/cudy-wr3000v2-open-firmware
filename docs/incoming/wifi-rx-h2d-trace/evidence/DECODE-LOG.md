# DECODE-LOG - exact commands, hashes and results for the wifi-rx-h2d-trace lane

Lane: rx-h2d-trace (shared task-176), 2026-11-27. Host-only; no device contact.
All module/firmware inputs are LOCAL artifacts (gitignored), never committed. Everything below is
reproducible from a copy of the vendor rootfs and the project toolchain.

## 1. Inputs (local)

```
KO /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/
  hi5622v100_wifi.ko   3,581,748 B  sha256 22fa789a...532c7d2  (unstripped, 17,059 symbols)
  hi5622v100_plat.ko   (HCC/BAL/ETE transport; hcc_msg_alloc 0x11d9c, hcc_msg_tx 0x11bf8,
                        hcc_msg_tx_to_core 0x11b2c, hcc_queue_info_dump 0x124a8, ...)
  wifi_debug.ko         437,500 B  sha256 b5111503...90ef1995
  plat_debug.ko          99,304 B  sha256 583b8d8a...7b453f6
FW /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
  928,920 B sha256 7fc87e20...c3ef311b ; CPU address = file offset + 0x40000
TOOL /home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/
  arm-openwrt-linux-objdump/readelf/nm/gcc (+ capstone 5.0.7 in .provision-venv)
```

The pair-A 2.5.24 copies of the modules are re-hashed on the device in RUNBOOK T2; the decode
above is the 2.4.15 rootfs copy of the same vendor module family (the symbol/offset anchors were
independently re-verified against the 2.5.24 module by the committed lanes cited in REPORT.md).

## 2. Module identity / surface decode (wifi_debug.ko, plat_debug.ko, plat.ko)

```
$TOOL/arm-openwrt-linux-readelf -sW wifi_debug.ko | grep -E 'hcc_|oal_pcie|hcc_test|wal_hipriv'
$TOOL/arm-openwrt-linux-readelf -p __ksymtab_strings hi5622v100_plat.ko | grep -i hcc
    -> hcc_set_state hcc_get_state hcc_init hcc_get_instance hcc_msg_alloc hcc_msg_free
       hcc_msg_tx hcc_msg_tx_to_core hcc_msg_register_tab{,_customise,_chip,_core}
       hcc_msg_clone hcc_queue_info_dump hcc_queue_register_customer* hcc_timer_*
$TOOL/arm-openwrt-linux-objdump -dr -j .text -j .text.unlikely wifi_debug.ko > wifi_debug.dis
$TOOL/arm-openwrt-linux-objdump -dr -j .text -j .text.unlikely hi5622v100_plat.ko > plat.dis
$TOOL/arm-openwrt-linux-objdump -s -j .modinfo <ko>     # license/depends/vermagic/parmtype
```

Decoded results (all cited in TABLES-debug-interfaces.md):

| finding | anchor |
| --- | --- |
| `save_hostmem` = host-physical ioremap + vmalloc + file dump | wifi_debug.dis `00000f04 <oal_pcie_debug_save_hostmem>` (`sscanf_s` 3 fields at +0xf54, `ioremap` +0xf90, `vmalloc` +0xfa0, `filp_open` +0xfb8, 4-byte `ldr`/`str` loop +0xff4..+0x1018) |
| `hcc_queue_info_dump` is a no-op stub | plat.dis `000124a8 <hcc_queue_info_dump>: mov r0,#0; bx lr` |
| base `/sys/hisys/plat/debug` is a stub | plat.dis `0001a554 <plat_debug_sysfs_set_info>` (mov r0,#0; bx lr) / `0001a55c <plat_debug_sysfs_get_info>` (oam_warning_log0) |
| wifi_debug's `hcc_test` facility registers its own D2H handler table and a workqueue | wifi_debug.dis `00005ed8 <hcc_test_sysfs_init>` -> `hcc_msg_register_tab` +0x5fc4, `alloc_workqueue` +0x5fe8 |
| wifi_debug/plat_debug duplicate the same debug source family (load one only) | both modules' symtabs: `oal_pcie_debug_*`, `hcc_test_*`, `oal_plat_test_*` |
| `hcc_msg_alloc`/`hcc_msg_tx` header pointer at object+0x118; total = requested+12 | plat.dis `00011d9c <hcc_msg_alloc>` (`ldr r2,[r4,#0x118]`, `strh r5,[r2,#4]`), `00011bf8 <hcc_msg_tx>` (`ldr r3,[r4,#0x118]`, `strb r2,[r3,#9]`) |

## 3. Firmware name-pool negative (F1 narrowing, space 4)

```
$ strings -a -t x FIRMWARE.bin | grep smac_msg_proc   -> 13 strings, file 0xE2824..0xE29C8
  incl. smac_msg_proc_rx_ring_reset_req @ 0xE28BC, smac_msg_proc_enable_trx_req @ 0xE29A8
$ python scan: for every u32 LE word W in the file, W - bias in [0xE266C..0xE2C98]
  for bias in {0, 0x10000, 0x20000, 0x40000, 0x80000, 0xC0000, 0xE0000} -> 0 pointer hits
```

This re-confirms the committed phase-8 result: the 45-name `smac_*` tail name pool has no pointer,
no index array and no computed-base reference anywhere in the image, so the
`rx_ring_reset_req`/`enable_trx_req` handlers cannot be reached by name. The remaining static
route (an SMAC opcode dispatcher -> H2D dispatch mapping) stays [U]; the stock trace is the
discriminator (REPORT.md section 6).

## 4. The H2D trace instrument grounding (already-proven mechanisms)

* SR ring read-only read on a live vendor boot: `opensource/docs/phase25/live-vendor-ring-ground-truth.md`
  (SR ch0 base 0x848F6000, 8-byte nodes {buf,len}, buffers read through /dev/mem).
* SR register map / live values: `opensource/docs/phase20/tx-path.md` A.3/A.7 (base +0x10, depth +0x14,
  producer +0x18, consumer +0x1c; ETE window host 0x403f2000; node flags 0x6d2b).
* tracefs + dynamic kprobes functional on the stock 5.10.201, kptr_restrict=0, only the `nop`
  static tracer: `opensource/docs/phase3/wire-capture.md` sections 1.2-1.4.
* one-shot S08 init aid arming probes inside the module-load window: `opensource/docs/phase21/live-binding.md` A.0.
* HCC header / dispatch / id spaces: `opensource/docs/incoming/wifi-driver-re/REPORT.md` section 3,
  `phase24/hcc-command-layer.md`, `phase25/chip-message-tables-named.md`,
  `wifi-rx-init-msg/TABLES-h2d-builders.md`.
* 5.10 kprobe fetcharg recursion (needed by the h2d_tx nested-deref probe): checked in the local tree
  `build/linux-kbuild/linux-5.10.201/kernel/trace/trace_probe.c` (`parse_probe_arg` recurses on '+')
  and `Documentation/trace/kprobetrace.rst`.

## 5. Host verification (this lane's deliverables)

```
$ opensource/docs/incoming/wifi-rx-h2d-trace/tools/run-host-tests.sh
  -> decode_h2d.py --selftest PASS
  -> sampler fixture: 6 valid / 2 quarantined / 5 families / 0.5 s; buckets matched=2 drift=1
     missing=1 (key 0x0004) core=1 (g1) g2=1
  -> kprobe fixture: 2 valid messages
  -> bulk fixture (save_hostmem path): 2 valid messages
  -> device sampler cross-build: static ARM EABI5 ELF, 50,116 B stripped
  -> ALL HOST TESTS PASS (14 [PASS] lines, 0 [FAIL])
  transcript: evidence/host-tests.txt

$ opensource/docs/incoming/wifi-rx-h2d-trace/tools/build-sampler.sh
  CC=<openwrt musl gcc 14.4.0>  STAGING_DIR=<clone>/staging_dir
  build/wifi-rx-h2d-trace/omo_h2dsampler: ELF 32-bit LSB executable, ARM, EABI5, statically linked
  50,116 B stripped  sha256 317d1f4122176fc89329fa1c0596771c7255d1e3ca9ca81cbaabc75e255fa714
  (local artifact, gitignored, never committed)
```

## 6. What was NOT done by this lane

* no device contact, no module load, no fire, no stock boot (the Lead's RUNBOOK);
* no vendor binary/firmware byte stored in the repo; the only protocol bytes in the lane are the
  committed analysis exemplar (the 0xD8 config event geometry, already public in the repo's
  wifi-rx-init-msg/phase-20 docs) and synthetic test vectors built from documented fields;
* no claim that the trace closes the RX bug - it closes RX-1 and yields the RX-2 candidate list.
