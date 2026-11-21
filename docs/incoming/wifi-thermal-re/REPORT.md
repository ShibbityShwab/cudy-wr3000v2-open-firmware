# Wi-Fi + thermal feasibility and scope report — Cudy WR3000 v2.0 (HiSilicon luofu R116)

**Lane:** `wifi-thermal-re` (host-only). **Written:** 2026-11-21.
**Scope:** definitive feasibility/scope for (1) the Hi5622V100 Wi-Fi stack, (2) the Hi5671Y
`tvsensor` thermal block, (3) a scan of other unsupported/missed blocks in the vendor DT/rootfs.
**Target context:** the project's 6.18.60 `hisilicon/luofu` target skeleton (patches 001-062) and
the OpenWrt 22.03.6 / vendor-5.10.201 stock image.

**Method / limits.** Local files only: the extracted vendor rootfs
(`/home/shibbityshwab/recovery-extract`, byte-identical module set to the parent repo's
`rootfs-2.4.15/`), the project's own flash dumps (`dumps/router-20261108/full/`), the repo's
calibration snapshots and UART boot capture, the tracked reverse-engineering docs, plus fresh host-side
`readelf`/`nm`/`objdump`/Ghidra analysis of the shipped modules on 2026-11-21. **No device was
touched**; no router command, UART session, reboot, flash or module load was performed. **No vendor
binary, firmware blob or per-unit value is committed here**; local artifact paths are given so an
owner of the unit can re-derive each fact, and per-unit calibration/MAC/serial/die-id values are
withheld.

**Evidence markers** (project convention):
**[V]** reproduced/re-verified by this lane from a named local artifact (host-side, read-only);
**[V\*]** measured on the device by an earlier attended lane and recorded in the named repo doc (not
re-run here); **[I]** inference; **[U]** unknown/unmeasured; **[X]** absent or blocked.

**Index**

| question | short answer | section |
| --- | --- | --- |
| Wi-Fi driver modules/firmware | 2 modules (`hi5622v100_plat`, `hi5622v100_wifi`) + 928,920-byte closed `FIRMWARE.bin` + 2 INIs + per-unit fac-partition calibration | §2.2, §2.3, §2.5 |
| firmware load interface | host writes the whole blob to device CA `0x01240000` (BAR0+0x6f8000) with `bal_write`->`pcie_write`->`memcpy_s`; then writes `0x5a5a` to CA `0x40000108` (CPU release); then IRQ enable and two firmware HCC ready messages. **No rings/doorbell in the firmware transfer.** | §2.4 |
| calibration / "EFS" format | no EFS exists; persistent store is the `fac` MTD5 partition mounted at `/usr/local/factory`, holding two `ZZZZ`-container blobs (8912 B / 2336 B) that are verbatim driver structs, plus an optional text `.cal`; static config is the INI | §2.5 |
| full 6.18 driver port | blocked on vendor GPL source; 10-16 engineer-weeks after source (host) + bench margin; upstream-impossible (closed firmware, vendor userspace ABI) | §2.6 |
| partial AP/client without vendor source | **not reachable on 6.18**; only the vendor 5.10 module stack works (hybrid fallback); clean-room ceiling not raised by this lane | §2.7 |
| thermal sensor | vendor DT **enables** it (the "vendor DT disables it" claim is false); vendor driver enables + trims + reads it; our 6.18 driver used the wrong variant/register/formula and no enable sequence, hence raw 0 and ENODATA | §3 |
| validated thermal driver | small: correct v1/hwver-3 path (read `+0x04[9:0]`, formula below), the vendor enable/trim sequence, widened register window; ~2-4 host days + 1 attended bench session | §3.4 |
| USB / RTC / I2C / PWM / NFC | USB **absent by design on this board** (no DT node, no modules; dead init-script refs); RTC **absent**; I2C present in vendor DT + DW modules but unused and unloaded; PWM: PMU-PWM used for CPU `vdd_logic`, LED-PWM has a DT node but no driver/consumer; NFC **absent** | §4 |

---

## 1. Where the evidence comes from

### 1.1 Local artifacts used in this lane

| artifact | location (local, gitignored) | what it establishes |
| --- | --- | --- |
| vendor rootfs 2.4.15 (modules, firmware, INIs, scripts) | `/home/shibbityshwab/recovery-extract/` (= repo `rootfs-2.4.15/`) | module metadata, strings, init scripts, factory mount |
| full MTD dump set (16 partitions + `proc_mtd.txt`) | `dumps/router-20261108/full/` | base DTB + board overlay extraction, `fac`/MTD layout, boot chain inputs |
| stock boot UART capture | `build/uart-logs/20261107T191003Z-poweron-uboot.log` | PCIe enumeration, BAR assignment, firmware download, ready messages, netdev/VAP creation |
| calibration snapshot (2g/5g `.kv`, INIs, alg surface) | `build/linux-kbuild/cal-snapshots/25691117-005835/` | per-unit store sizes/container format, live `alg` surface |
| repo RE record | `opensource/docs/DRIVER-BLACKBOX.md`, `PORT-SURFACE.md`, `phase4-27`, `phase42/43`, `soc/*`, `REMAINING-DRIVERS.md` | prior device-measured facts ([V\*]) and decoded maps |
| current 6.18 port artifacts | `target-skeleton/dts/luofu-r116.dts`, patch `012-arm-thermal-luofu-tvsensor.patch`, `lab/luofu-thermal/luofu-thermal.c` | what the target currently does (and where it is wrong) |

### 1.2 Host-side reproduction tools

`arm-none-linux-gnueabihf-{readelf,nm,objdump}` from `build/linux-kbuild/toolchain/bin/`;
Ghidra 12.2 headless (`/opt/ghidra_12.2_PUBLIC`, `tools/ghidra/DumpFuncs.java`) for the small
thermal module. All addresses below are **module link-time offsets** (section `.text` vaddr 0), which
is also the form the existing repo docs use, so the two are directly comparable. Vendored-module
identity for this lane: `hi5622v100_plat.ko` md5 `f688618b4daa52473be41577f992f7d4`,
`hi5622v100_wifi.ko` md5 `1745ef77e728c411cef1921af3dc8731`, `hi_tvsensor.ko` md5
`80e279c5ec260e1653b5a8854e043e07` (2.4.15 set; the 2.5.24 set in some older docs has different
md5s but the same function layout contract).

### 1.3 The vendor DT, re-extracted in this lane

The base DTB and both U-Boot overlays were re-extracted from `mtd11-kernela.bin` by FDT-magic scan
and decompiled with `dtc` (host-side):

| blob | offset in mtd11 | size | sha256 (blob) |
| --- | --- | --- | --- |
| base DTB | `0x418610` | 31,220 | `947ec62d7fba2aed624166a585a5001f5385befba962f071f19ed468dfa270e3` |
| overlay board_id=0 | `0x420064` | 4,592 | `fe7df5ea…` |
| overlay board_id=3 (this board, R116) | `0x421254` | 4,920 | `17fd44ed…` |

This matches `opensource/docs/soc/vendor-dt-notes.md` and the pinned DTS; it is the authoritative
"what the vendor DT says" artifact used in §3.

---

## 2. Wi-Fi: Hi5622V100

### 2.1 Silicon, bus and topology

| item | value | evidence |
| --- | --- | --- |
| radio | Hi5622V100, vendor codename **ShuangtaV100**; chip id `0x34` / rev `0x00`; firmware banner `ChenTangV100R001C20T13` | module strings; `iwpriv …get_chipid` recorded in DRIVER-BLACKBOX [V\*]; FIRMWARE.bin strings [V] |
| bus | PCIe, **two endpoint functions**, one per band, behind two DesignWare root complexes | vendor DT `pcie@0x10160000` / `pcie@0x10164000`; stock boot log lines 619-654 [V\*]/[V] |
| endpoints | `0000:00:00.0` and `0001:00:00.0`, both `[59e7:0005]`, class `0x028000`; vendor module `pci_device_id` table also carries `59e7:0002` (sibling part) | boot log 626/645 [V\*]; plat.ko `.data` PCI-ID table at file `0x20478`: `e7 59 00 00 02 00…`, `e7 59 00 00 05 00…` [V] |
| BARs (domain 0) | BAR0 `0x40000000-0x40ffffff` (16 MiB), BAR4 `0x41000000-0x417fffff` (8 MiB), BAR2 `0x41800000-0x41803fff` (16 KiB), 64-bit | boot log 633-635 [V\*] |
| BARs (domain 1) | BAR0 `0x58000000-0x58ffffff`, BAR4 `0x59000000-0x597fffff`, BAR2 `0x59800000-0x59803fff` | boot log 652-654 [V\*] |
| cross-wiring | `is_pcie_cross=1`; RC0 host view reports phy_devid `0x11521`, RC1 `0x11520` (so domain ≠ band by assumption) | boot log 835-850 [V\*] |
| interrupts | MSI capable (count 1/1) but the vendor uses **INTx** (IRQ 207 / 209) | DRIVER-BLACKBOX §1 [V\*], boot log |
| endpoint driver split | `hi5622v100_plat.ko` = PCIe/BAL/HCC/INI/firmware layer (371,708 B); `hi5622v100_wifi.ko` = MAC/MLME/cfg80211 (3,581,748 B) | §2.2 |

The endpoint's two endpoints appear at boot as two independent radios (`phy0` 2.4 GHz,
`phy1` 5 GHz), each with four VAPs and bridged into the LAN; the stock boot log shows
`Hisilicon0` netdev then `wlan00..wlan03` / `wlan10..wlan13` cleanup/create at lines 892-1019
[V\*].

### 2.2 The vendor driver modules (structure and size)

Measured on the shipped 2.4.15 modules in this lane unless marked otherwise:

| property | `hi5622v100_plat.ko` | `hi5622v100_wifi.ko` |
| --- | --- | --- |
| size | 371,708 B | 3,581,748 B |
| md5 / sha256 | `f688618b4daa52473be41577f992f7d4` / `27c69791…dbba27` | `1745ef77e728c411cef1921af3dc8731` / `22fa789a…32c7d2` |
| ELF | ARM32 relocatable, not stripped; `vermagic=5.10.201 SMP mod_unload ARMv7`; `license=GPL`; no `.plt/.init.plt`, no `__versions` (no MODVERSIONS) | same |
| declared dependency | none | `depends=hi5622v100_plat` |
| `.text` | 119,588 B | 1,452,124 B |
| `.rodata` / `.data` / `.bss` | 5,219 / 11,581 / 20,196 B | 151,940 / 92,008 / 317,712 B |
| code symbols (`nm -S`, type `t`) | 704, summed size 118,680 B | 4,247, summed size 1,299,812 B |
| undefined imports | 126 | 308 |
| cfg80211/wiphy imports | 0 | **25** |
| mac80211 imports | 0 | **1** (`ieee80211_get_channel_khz`) |
| namespaced imports | 46 × `import_ns=HW_RTOS_NS` modinfo tags | 211 × `import_ns=HW_RTOS_NS` tags |
| big layers (symbol-name prefixes, wifi.ko) | `oal_`, `pcie_`, `hcc_`, `bal_` | `hmac_` 2,023, `wal_` 1,014, `mac_` 367, `alg_` 239, `hdpp_` 238, `shuangta_` 147, `hal_` 99, `oal_` 43, `oam_` 33 (all symbol names, not only functions) |

Interpretation (corrects older docs where noted in §6):

* This is a **cfg80211 full-MAC driver**, not a mac80211 driver: it registers its own netdevs and
  wiphys (`wiphy_new_nm`/`wiphy_register`, `alloc_netdev_mqs`), and imports only one mac80211
  helper. The MLME/rate/aggregation/DFS logic is vendor C inside the module (4,247 functions).
* The **`import_ns=HW_RTOS_NS` count is a per-import modinfo tag count, not a unique-symbol
  count.** All 308 undefined symbols in wifi.ko and 126 in plat.ko are ordinary kernel/vendor-module
  imports that the stock boot resolves; no mainline kernel exports a namespace provider. The practical
  consequence is unchanged: the modules are built against the vendor 5.10 kernel and its shipped
  companion modules, and cannot be re-linked against a mainline 6.18 tree.
* **Runtime closure is wider than `depends=`.** `hi5622v100_wifi.ko` also uses runtime
  `__symbol_get` hooks into `hi_woc_wifi_rtx.ko` (5 `hi_wifi_*` hooks) and `hi_kcfe_lrn.ko`
  (2 `hi_cfe_flush_*` hooks); both are loaded by the stock init before Wi-Fi. `hi5622v100_plat.ko`
  additionally uses `ksecurec` and `hi_kpie`. A port must carry or replace these too (documented in
  `PORT-SURFACE.md`, `REMAINING-DRIVERS.md` §4). [V\*]
* The modules have **no ARM module PLT sections** and declare no symbol versions, so the vendor
  kernel could be built without those features; this is why the hybrid on the vendor 5.10 kernel
  needed a `.plt` objcopy workaround (VENDOR-STACK-ON-MAINLINE.md) while a 6.18 load is simply
  impossible (vermagic + struct ABI). [V\*]/[V]

### 2.3 Firmware and static data files

| file | size | sha256 | role |
| --- | --- | --- | --- |
| `/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B (`0xE2C98`) | `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b` | closed radio firmware: Thumb-2 code + tables, uncompressed (max 4 KiB entropy 7.256), header words `w0=0x00046971` (Thumb entry point 0x46970), `w1=0x000C742D` (data table); banner `ChenTangV100R001C20T13`; test tokens `VERIFY20M`/`VERIFY40M` |
| `/lib/firmware/hi_wifi/cfg_device_hisi.ini` | 799 B | `34d9ce145d8d0ef91068d5e9b66e3a4ed001b1d182bdb2eb0288c755a5ad5f5e` | per-device board config: TCXO selection (`dev_tcxo_selection=0x2`), IDDQ/temperature model coefficients, and the "boundary" firmware segment table (`firmware_itcm_src_addr/len`, `firmware_dtcm_*`, `firmware_plat_custom_*`, `firmware_wifi_custom_*`) |
| `/lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini` | 32,315 B | `a7ac5bcd9adf8c00fd004a113827ad85ae592da5738ca3c94afa30768ef44297` | static radio configuration: `[HOST_WIFI_NORMAL]` (~186 keys, incl. a second firmware segment table and temperature/power compensation), `[HOST_WIFI_PWR_LIMIT_LU]` (per-channel/rate power limits), `[CUSTOM_REGDOMAIN_CFG]` (country band rows) |
| FIRMWARE licence/redistribution text | — | — | **none found** in `/lib/firmware`, `/usr/share/licenses` or anywhere else in the rootfs [V] |

Notes for a port:

* The firmware blob is byte-identical between stock 2.4.15 and 2.5.24 and identical to a sibling
  Jeton AX3000 build (same md5), i.e. it is an SDK image, not a per-release artifact ([V\*] for the
  release/sibling comparison; this lane re-hashed the 2.4.15 copy [V]).
* The INI "segment" addresses are **chip address-space values, not file offsets** — e.g.
  `firmware_itcm_src_addr=0xf009c` (983,196) is larger than the 928,920-byte file, and the
  `cfg_device_hisi.ini` "boundary" set has different values (`0x117bc4`, `0x12252c`,
  `0x1b2800`) written by the vendor build script. They are parsed by the host INI layer (the
  `firmware_*` key strings are present in plat.ko) but the **host transfer does not segment the
  file** (§2.4); their exact boot-time use is **[U]** (likely device-side relocation/segment metadata;
  not needed to reproduce the byte transfer).
* The INI parser emits many non-fatal "not find var" warnings on this unit for optional DPD/temperature
  keys; that is expected, not a broken config (stock log lines 656-732 [V\*]).

### 2.4 Firmware load interface, decoded (with a correction)

All addresses/relocations below were re-verified in the shipped **2.4.15** `hi5622v100_plat.ko`
in this lane; they match the earlier 2.5.24 dump in `opensource/docs/phase17/fw-download.md`
and `phase18/inbound-map.md`, which remain the full-length treatment.

#### 2.4.1 Call chain (verified by `bl` targets in `firmware_download`)

```
wlan_power_on                         @0x00e46c
  0x00e494: bl wlan_bal_init_process          (dev_power_ctl/bal_reinit; PCIe stubs on this chip)
  0x00e4b0: bl hwifi_rf_cali_file_load        (reads the fac-partition .kv calibration, §2.5)
  0x00e4d0: bl firmware_download_function     @0x00f9f8
  0x00e4f0: bl plat_init_bal_hcc_excp         (installs the 5-entry chip HCC message table, @0x00e380)
  0x00e5f8: bl bal_irq_enable                 (host-side enable_irq only; no device write)

firmware_download_function            @0x00f9f8
  0x00fab8: bl firmware_download             @0x00f834
firmware_download                     @0x00f834
  0x00f860: bl firmware_check_version        (stock log: "version check skip")
  0x00f8d0: mov r1,#0x1000                    ; alignment
  0x00f8d8: mov r0,#0x80000                   ; bounce buffer size 524,288 B
  0x00f8dc: bl firmware_mem_try_alloc
  0x00f930: bl firmware_file_send             @0x00f56c
firmware_file_send                    @0x00f56c
  0x00f5e8: filp_open / vfs_llseek            ; /lib/firmware/hi_wifi/FIRMWARE.bin
  0x00f6b4: firmware_kernel_read              (one chunk into the bounce buffer)
  0x00f724: bal_write                         @0x010c04  -> g_st_pcie_bus_driver[0x38] = pcie_write
pcie_write                            @0x00bdc0
  0x00bdf4: pcie_para_check -> oal_pcie_inbound_ca_to_va @0x008dd8
  0x00be10: memcpy_s(dst = host VA of the device CA, src = bounce buffer, len)
```

`bal_write` is a **bus dispatch**, not a ring/doorbell path: `pcie_write` translates the target
device CA to a host VA through the six pre-programmed inbound region descriptors and performs a
plain `memcpy_s`. There is no ETE descriptor, no SR channel, no doorbell and no message header in
the firmware transfer. (This refuted the earlier "rings carry the firmware" model in phase 17 and
remains correct.) [V]

#### 2.4.2 PCIe/BAR side: how the target address becomes a host pointer

The vendor endpoint driver programs the root complex and the endpoint's inbound windows before the
download:

* **RC/iATU programming** (2.4.15 addresses): `oal_pci_lres_init @0x7bb0` →
  `oal_pcie_host_init @0xbefc` (reads endpoint cfg revision byte, config word `0xff8`) →
  `oal_pcie_dev_init @0xa090` → inlined `oal_pcie_set_inbound @0x989c`.
  The endpoint revision on this silicon is 0, so the code takes the **BAR2-membar branch**, not the
  config-space `0x900` viewport path: it `ioremap`s BAR2 and writes 16 possible viewport register
  blocks at **BAR2 + 0x104 + 0x200*i** (six used): `CTRL2 = enable | BAR number`,
  `base_lo/hi`, `limit_lo/hi`, `target_lo/hi`. [V]/[V\*]
* **Six inbound regions** map device CA ranges to host windows (region table
  `g_shuangta_region_types @ .data+0x29d8`, 6 × 0x50 bytes): 0 `0x00000000-0x001bffff`
  (`ROM_WRAM`), 1 `0x00400000-0x00417fff` (`TCM_NOACP`), 2 `0x01000000-0x011dffff`
  (`PKTRAM_NOACP`), 3 `0x40000000-0x4011ffff` (`IO`), 4 `0x02000000-0x021dffff` (`ACP`),
  5 `0x01200000-0x01417fff` (`ACP`). [V]/[V\*]
* **Firmware target:** `firmware_download_function` sets the file table entry to
  `{path="/lib/firmware/hi_wifi/FIRMWARE.bin", target=0x01240000}` (the path pointer and
  `0x01240000` are the two words at `.LANCHOR1` = `.data+0x269c`/`+0x26a0` [V]). CA
  `0x01240000` falls in region 5, whose measured host base is BAR0+0x6b8000, giving
  **BAR0 + 0x6f8000**; the same SRAM is aliased at **BAR0 + 0x40000**, where the firmware image was
  independently observed byte-for-byte after loading (phase 17/18, device-measured [V\*]).
* **Chunking:** the bounce buffer is 0x80000 B; `firmware_file_send` sends
  `min(0x80000, filesize)` per iteration while maintaining a cumulative offset. For this image:
  chunk 0 = 524,288 B at CA `0x01240000`, chunk 1 = 404,632 B at CA `0x012C0000`.
  The transfer is raw file bytes; there is no per-chunk header/alignment beyond the 8-byte-aligned
  bounce buffer. [V]

#### 2.4.3 The boot handshake (and the CPU-release write)

After the last chunk is written, the vendor's `firmware_download` **does** execute a single-shot
device write and then returns:

```
0x00f914: ldr  r3,[r4,#0xc]      ; file count (1)
0x00f918: cmp  r6,r3
0x00f91c: bcs  0x00f97c          ; all chunks sent -> done
...
0x00f97c: bl   kfree             ; free bounce buffer
0x00f984: b    0x00f884          ; -> single-shot descriptor path
0x00f884: ldr  r3,[r4,#0x14]     ; optional per-chip callback (NULL for type 0)
0x00f88c: beq  0x00f8a0
0x00f8a0: ldr  r2,[r4,#0x18]     ; single-shot descriptor = .data+0x26a4
0x00f8a4: ldr  r0,[r4,#4]        ; chip/port handle
0x00f8a8: ldr  r3,[r2,#8]        ; len = 4
0x00f8ac: ldr  r1,[r2],#4        ; dev addr = 0x40000108, r2 -> value word
0x00f8b0: bl   bal_write         ; writes 0x00005a5a to CA 0x40000108
```

The descriptor at `.data+0x26a4` is exactly `{ 0x40000108, 0x00005a5a, 0x00000004 }` and was
read from the shipped module's `.data` in this lane [V]. **Correction to
`phase19/fw-boot.md` §A.0/A.4:** that document concluded the vendor does not take this branch
for chip 0. It does: the chunk loop falls through to the single-shot write after the last chunk.
The write is the chip-CPU release kick, independently proven by the phase-19b experiment (writing
`0x5a5a` to CA `0x40000108` made the chip leave ROM state: firmware BSS zeroed, analog/status
registers went live, first mailbox word `CA 0x40039014 = 4` within ~3 s). [V\*] The stock boot log
is consistent: "finish download firmware" → "Device plat ready" within ~20 ms (lines 863-867).

Then:

1. `plat_init_bal_hcc_excp @0x00e380` installs the 5-entry chip message handler table at
   `.data+0x2660` (entries: id 0 —, id 1 `device_plat_ready_msg_process @0x00eb78`, id 2
   `host_ready_msg_process @0x00ebd8`, ids 3/4 —). [V]
2. `bal_irq_enable @0x010c78` → `pcie_irq_enable` → `enable_irq` on the endpoint IRQ line; this
   is host-side only. [V]
3. `multi_chip_loading @0x00f2ac` waits twice with `wait_for_completion_timeout`:
   **200 jiffies** for the "platform ready" HCC message (id 1), then **2000 jiffies** for the
   "device ready / host ready" message (id 2), whose payload is copied to
   `g_dmac_to_hmac_read_msg[core]` (0x64-byte records) and whose ready-bitmap is updated. The
   stock boot log contains the first handler's string verbatim: `Device plat ready! chip id : 0`
   (line 867). [V]/[V\*]
4. Only after that does wifi.ko run `hal_main_init` → `hmac_main_init` → `wal_main_init` and
   register wiphys/netdevs. [V\*]

#### 2.4.4 Rings and the message service (what is *not* used for loading)

* The **ETE SR/DR DMA rings** are the HCC **message** path, not the firmware download path. Channel
  blocks live at device CA `0x4003a000` (7 × 0x50-byte channels: SR0-2, DR0-3), host window
  BAR0+0x3b8000; ring nodes are **8 bytes** (`word0 = device VA of buffer`,
  `word1 = (len<<16) | flags`, observed flag word `0x6d2b`); ring base/depth/head/tail registers
  live in the MAC host-interface block CA `0x40040000` (band-0 slot 5) at offsets
  `+0x20..+0x54`, with the 20-word channel array at `.rodata+0xb60` in wifi.ko. [V\*], maps in
  `phase42/THE-RING-MAP.md`, `phase43/THE-CHANNEL-MAP.md`.
* The **doorbell/mailbox** pair used by the message path is `out[0] CA 0x40039010` (pending mask)
  and `out[2] CA 0x400392d4` (doorbell); the firmware's first word after release appeared at
  `out[1] CA 0x40039014`. The host→device dispatcher never ran in the earlier from-scratch takeover
  line (the unresolved gate is documented in `phase22-27`, `phase49/README.md`; the last state is
  "line 0x4C pending/never consumed, SGI-bank competition"), so **no working data path** was ever
  reached by the clean-room line. That is a ceiling of the earlier lane, not a claim of this report.
  [V\*]
* **Implication:** the firmware transfer itself is simple and fully decoded; the ring/message layer
  matters only for everything that comes after boot (calibration delivery, scan/assoc, TX/RX).

### 2.5 Calibration and persistent storage (the "EFS" question)

**There is no EFS filesystem or EFS container in this vendor image [V].** A full scan of the rootfs,
the firmware directory and the shipped modules found no EFS volume, no `efs` file names and no EFS
string in the Wi-Fi modules. The persistent per-unit data is stored as ordinary files in the **`fac`
MTD5 partition**, mounted at `/usr/local/factory` by `/etc/init.d/hsan_prepare` (`do_mount fac
/usr/local/factory/`, line 95) [V]. The static radio configuration is the INI pair in §2.3. "EFS" in
earlier shorthand should be read as these vendor flat files / the INI firmware-segment map, not as a
file system.

| object | size | evidence |
| --- | --- | --- |
| `/usr/local/factory/wifi_cali_data.kv` (dual-band payload) | 8,912 B = `0x22d0` | snapshot file; writer count literal `0x22d0` in wifi.ko [V] |
| `/usr/local/factory/wifi_cali_data_2g.kv` | 2,336 B = `0x920` | snapshot file; writer count literal `0x920` [V] |
| `/usr/local/factory/hi5622v100.cal` | optional text | produced by `usr/bin/hi5622v100_cal_get.sh` from `iwpriv … alg get_*`; consumed by `hi5622v100_cal_init.sh` via `alg set_*`; empty/absent on the captured unit [V\*] |
| `/usr/local/factory/wifi.cal` | optional text | read by `wifi_cal_init.sh`; empty on the captured unit [V\*] |

**Container format** (decoded in `phase8/kv-format.md`, re-checked here against the snapshot):

* On disk the file is a **verbatim image of a driver RAM struct** written by one
  `oal_kernel_file_write` (`hmac_save_cali_data_to_file_2g/_5g`, wifi.ko `0xc3f4c`/`0xc3e54`;
  verifier helpers `hmac_save_cali_data_verify_2g/_5g` at `0xc3d3c`/`0xc3c20`).
* The verifier rewrites a `0xfeedfeed` sentinel at RAM offset 0 and at `len-4` to
  **`0x5a5a5a5a` ("ZZZZ")** and **`0xa5a5a5a5`** before writing, so the disk image is
  `ZZZZ | payload (len-8 bytes) | a5a5a5a5`. Trailer offsets: dual `0x22cc`, 2g `0x91c`.
* The reader (`hwifi_rf_cali_file_load_2g/_5g` in plat.ko) allocates the exact size, reads it,
  and rejects unless `word[0]==0x5a5a5a5a` and `word[last]==0xa5a5a5a5`; the stock boot log shows
  `{hwifi_rf_cali_file_load_2g succ!}` and `…_5g succ!` (lines 857-858) [V\*].
* The 2g and dual payloads are **different objects** (not a prefix/nesting): 6,576 B difference,
  common prefix only 5 bytes, no whole power-table word from the live `alg get_*` answers appears
  in either file. The field-by-field struct layout beyond the container is **[U]** (writer copies an
  opaque struct; no DWARF).
* At boot the driver loads the stores before the firmware download; after boot the log shows
  `2g the cali result to device succ` / `5g the cali result to device succ` (lines 894-895),
  i.e. the host sends the calibration to the firmware over the HCC message path [V\*]. The
  userland text `.cal` path is a second, redundant hand-off (only if the file exists).
* Live read-back surface: `iwpriv Hisilicon0 alg get_*` exposes 2g/5g power + low-power params,
  2g/5g all-curve params, XO ppm/duty, RSSI params (the captured unit has 10 zero RSSI words);
  other `alg` commands answer FAIL/arity errors, proving the parser is strict. Per-unit values are
  not reproduced here (redaction rule).

**Port consequence:** a 6.18 driver must (a) read the two `.kv` blobs from the fac partition and
send them to the firmware with the same boot-time protocol, or at least (b) carry the userspace
`alg set_*` path. The container is easy; the **calibration message content/format and firmware-side
application are closed** (see §2.6).

### 2.6 What a full 6.18 driver port requires

The honest decomposition (numbers measured in this lane's module metadata and the repo's 6.18 export
comparison in `target-skeleton/docs/incoming/wifi-recon/REPORT.md` §5.2):

| workstream | content | estimate (engineer-weeks, host) |
| --- | --- | --- |
| 0. Source gate | vendor GPL sources for `hi5622v100_plat`/`_wifi` + their kernel patch set/build config; GPL request sent 2026-10-01, no reply, **optional per project decision**; firmware redistribution terms still unknown | external |
| 1. PCIe host controller | finish/validate the 6.18 DesignWare host driver (patch 013 + reset polarity patch 014; host-compiles, **never booted**); needs attended enumeration of both `59e7:0005` functions, BAR assignment, misc-window write-only rule, aligned-dword accesses | 2-4 |
| 2. Kernel API port | wifi.ko has 308 undefined imports (plat.ko 126); of those the earlier lane counted 133/117 *kernel-library* imports, and the 6.18 export check found 11 wifi / 16 plat names no longer exported under the same name (printk wrappers, skb inline wrappers, kmalloc noprof, `kern_path_create`, `hrtimer_setup`, `alloc_workqueue` noprof, `dma_sync_single_for_cpu`, `skb_queue_purge_reason`, `sprint_symbol`, `mem_section`, …); every netdev/skb/pci_dev/cfg80211 struct ABI moved | 2-4 |
| 3. cfg80211 API port | 25 cfg80211/wiphy imports + 74 `wal_cfg80211_*` callbacks; binary built against OpenWrt backports v5.15.92 with CONFIG_PM=n; 6.18 moved notification/ext variants and added MLO-era struct fields | 1-2 |
| 4. Firmware/calibration/userspace | request_firmware path, INI parser, `.kv` store + boot-time calibration message, optional text `.cal`, vendor netifd `/sys/hisys/hipriv` VAP ABI (or replace it with nl80211 add_virtual_intf), regulatory.db | 1-2 |
| 5. Bench bring-up | enumerate → firmware download → kick/release → two ready messages → wiphy registration → scan/assoc → data path; attended with recovery armed, UART capture, snapshot before writes | 2-4 |
| **total** | | **10-16 after source** + bench margin |

What is **blocked by closed firmware / vendor kernel** (cannot be solved by host-side RE alone):

* The **firmware image itself** (code, tables, boot/relocation semantics) and its
  redistribution/licence terms.
* The **device-side half of the host interface**: the H2D message dispatcher / accept gate and the
  TX-credit source. The clean-room lane proved the ceiling: firmware D2H messages and rings can be
  observed, but the host→device message service never ran (tx-credit comes from the chip layer;
  the last GIC/line-0x4C work left the SGI-bank issue open). Without this, beacons, scans, keys and
  data frames cannot be driven even if a wiphy registers.
* The **calibration application path** (host sends `.kv` to firmware; firmware converts to PHY
  tables) and the **algorithm/config command layer** (`alg get_*`/set_* semantics, 414-entry table).
* **RF/PHY bring-up** (power sequencing, PLL/TCXO, ADC/DPD cal, temperature compensation, per-rate
  power tables) lives behind the firmware + INI.
* The vendor userspace contract (`/sys/hisys/hipriv`, netifd patch, `hi_swa`/HSLINK/CFE hooks) —
  needed for the stock scripts, or must be replaced.
* Upstreamability: a GPL source port would still ship a closed firmware and vendor-shaped full-MAC
  code; it is not a mainline accept. **Per the project position, Wi-Fi remains [X] for the 6.18
  target.**

### 2.7 Is a partial AP or client realistically reachable without vendor source?

**On 6.18, no.** Criteria for "partial" (e.g. monitor/raw TX, a beacon-only AP, a scan-only STA) all
still require the firmware to be booted *and* the host↔firmware message service to be operational,
because the firmware owns PHY/MLME-side behaviour and the vendor full-MAC owns the rest. The earlier
from-scratch lane reached firmware placement, chip release, first mailbox word and ring/descriptor
experiments but **never a working H2D dispatcher or a data path**; the ring layout is not the gate.
Nothing in this lane's evidence shortens that.

**What is realistically reachable without vendor source:**

| route | state | effort | notes |
| --- | --- | --- | --- |
| vendor 5.10.201 kernel + vendor modules + current OpenWrt userland ("B2 hybrid") | **works today** (stock is exactly this); AP + STA + bridging verified in earlier lanes | 1-2 days per vendor release to keep alive | not 6.18, not upstreamable; module stack cannot be unloaded safely (exit-path NULL call → panic; phase15) |
| 6.18 kernel + vendor 5.10 modules | impossible | — | vermagic/ABI/no PLT; 6.18 loader refuses, and struct offsets moved |
| clean-room full-MAC on 6.18, no source | not realistic | years (programme-level) | firmware can never be opened; H2D service gate unresolved |
| vendor userspace on 6.18 | irrelevant to the radio | — | the radio is kernel-side; stock wpad is already the standard cfg80211 userspace |

**Recommendation:** keep the recorded position (Wi-Fi unsupported on the 6.18 target; do not ship a
vendor driver/blob); keep the PCIe host driver as independent enabling work; keep the vendor 5.10
hybrid as the only Wi-Fi-capable fallback, labelled as such. Re-open only if GPL source arrives; then
the sequence is reproduce-SDK-on-5.10 → port plat → wifi → calibration/userspace → bench gates.

---

## 3. Thermal: `tvsensor@14900500`

### 3.1 What the vendor DT actually says (the "disabled" claim is wrong)

The vendor base DTB extracted from the *boot partition* (`mtd11-kernela.bin` @ `0x418610`,
sha256 `947ec62d…`, the same tree U-Boot hands to the kernel) contains:

```dts
tvsensor0: tvsensor@14900500 {
    compatible = "hsan,tvsensor";
    hwver = <0x03>;
    reg = <0x14900500 0x14>;
    interrupts = <0x00 0x00 0x04>;          /* SPI 0 */
    #thermal-sensor-cells = <0x00>;
    trim = <0x00 0x00>;
    int-base-offset = <0x0c>;
    addition-en;
    efuse-trim-en;
    phandle = <0x38>;
};

thermal-zones {
    cls0: cls0 {
        polling-delay = <0x3e8>;            /* 1000 ms */
        polling-delay-passive = <0x64>;
        sustainable-power = <0x7d0>;
        thermal-sensors = <0x38>;
        trips {
            alert0: trip-point@0 { temperature = <0x1c138>; hysteresis = <0xbb8>; type = "passive"; };
            critical: trip-point@2 { temperature = <0x1e848>; hysteresis = <0xbb8>; type = "critical"; };
        };
        cooling-maps { map0 { trip = <0x39>; cooling-device = <0x3a 0 3>; contribution = <0x64>; }; };
    };
};

cooling_dev0 { compatible = "hsan,net-cdev"; ... };   /* the map's cooling device */
```

* The node has **no `status` property at all → enabled by FDT default** [V]. The board overlay
  `board_id=3` (R116) contains no `tvsensor`/`thermal`/`cooling` fragment (it only adds VAP
  keys/LEDs, PCIe GPIOs and disables gemac0/gephy0/lsw_dp); so the live DT is also enabled [V].
* The vendor kernel loads `hi_knet_cdev.ko` (cooling device) immediately before `hi_tvsensor.ko`
  in `/etc/rc.d/luofu` (lines 80-81) — a deliberate thermal stack, not a disabled block [V].
  (The vendor probe's enable/trim writes and the thermal-zone registration in §3.2 would be dead
  code if the node were disabled; they are consistent with an enabled block.)
* The `status = "disabled"` wording that circulated comes from **our own reconstructed DTS**
  (`opensource/docs/soc/luofu-r116.dts` ~line 469, and the lab DTS), not from the vendor tree. That
  is the subject of correction §6.1. [V]
* The vendor `reg = <0x14900500 0x14>` is **too small for the driver** (see §3.2: it reads
  `+0x04`, writes `+0x00`, `+0x04`, `+0x08`, `+0x0c`, `+0x10`, and writes `+0x400`). The
  vendor's `ioremap` of the platform resource is page-granular, which is why it "worked"; a mainline
  driver must declare a window covering every touched register (at least `0x14900500..0x14900908`),
  or split the `+0x400` addition-enable into its own resource. [V]

### 3.2 The vendor module, decompiled (Ghidra 12.2 headless, this lane)

`hi_tvsensor.ko`: 9,840 B, `.text` 2,820 B, 9840 B md5 `80e279c5…`, `vermagic 5.10.201 SMP
mod_unload ARMv7`, `license=GPL`, matches `hsan,tvsensor`, 22 imports incl.
`devm_thermal_zone_of_sensor_register`, `devm_request_threaded_irq`, `ioremap`,
`syscon_regmap_lookup_by_phandle`, `regmap_read/write`, `of_property_read_variable_u32_array`.
It has **two hardware-op tables**: a v1 table and a v4 table, selected in probe. [V]

**Probe (`hi_tvsensor_probe @0x0000024c`):**

1. `devm_kmalloc(dev, 0x3c)` state; `platform_get_irq(pdev, 0)`; `devm_request_threaded_irq`
   (IRQF_ONESHOT; handler `hi_tvsensor_irq_handler @0x1f8`, thread name "tvsensor").
2. DT property reads: `efuse-trim-en` (bool → +0x38), `trim` (2 cells; if cell0 != 0 the second
   cell is negated → +0x28), `hwver` (&0xf → +0x2c), `hwver_sub` (&0xf → +0x2e),
   `int-base-offset` (default 12 → +0x30), `addition-en` (bool → +0x34),
   `syscon` phandle (regmap; required only when hwver==4, ignored for hwver 3).
3. **Variant selection:** if `hwver == 4` → `hi_tvsensor_hw_v4_drv_register @0xaac`; **else
   (this board: hwver 3) → `hi_tvsensor_hw_drv_register @0x860` (v1 table).** [V]
4. Enable/configure sequence (function-pointer calls through the selected table, verified by
   decompilation and `objdump`):
   * `set_mode(base, 0)` — v1 writes `base+0x04` bit 27 = 0 (`hi_tvsensor_hw_set_mode @0x630`).
   * `set_int_mask(base, 1, int-base-offset=12)` — v1 writes bit 0 of `base+0x0c+4 =
     base+0x10` (`hi_tsensor_hw_set_int_mask @0x784`).
   * `set_trim(base, efuse_trim_en=1, trim=0)` — v1 `hi_tsensor_hw_set_trim @0x738`: with
     `efuse-trim-en` the code **clears** `base+0x04` bit 20 (select efuse trim); otherwise it
     sets bit 20 and encodes sign/magnitude of the DT `trim` value into bits 10-19.
   * `set_en(regmap, base, 1, addition_en=1)` — v1 `hi_tvsensor_hw_set_en @0x5e0`: writes
     `base+0x00` bit 24 = 1 (sensor enable), and because `addition-en` is present also writes
     **`base+0x400` bit 0 = 1** (separate block at `0x14900900`), then 10 delay loops
     (`arm_delay_ops`).
5. Registers the thermal sensor (`devm_thermal_zone_of_sensor_register`), sets up a work item for
   critical events, then reads the DT trip temperature through the thermal zone, programs the
   threshold (`set_th(base, trip-1, hwver)`), clears the interrupt mask and clears the interrupt
   latch. [V]

**Register semantics for this board's v1/hwver-3 path (all host-side decoded):**

| offset | width | meaning | writer/reader |
| --- | --- | --- | --- |
| `+0x00` | u32 | sensor enable: bit 24 = enable (1) | v1 `set_en` |
| `+0x04` | u32 | **temperature [9:0]**, trim [10..19] (sign/magnitude), bit20 trim-source select (0=efuse, 1=DT trim), bit27 mode | v1 `get_temp`, `set_trim`, `set_mode` |
| `+0x08` | u32 | threshold [9:0] | v1 `set_th` |
| `+0x0c` | u32 | interrupt clear (write 1) | v1 `clr_int` |
| `+0x10` | u32 | interrupt mask bit 0 (`int-base-offset` = 12, +4) | v1 `set_int_mask` |
| `+0x400` (=0x14900900) | u32 | "addition" enable, bit 0 (only when DT `addition-en`) | v1 `set_en` |
| v4-only offsets | — | temp `+0x24` (u32), mode `+0x10` bit1, threshold `+0x14`, int mask `+0x04`, clear `+0x00`, trim `+0x18/+0x20`, efuse syscon regmap access `regmap_read/write(0x708)` | v4 table only |

**Temperature conversion (decoded, exact C from Ghidra/decompiler, cross-checked against the
vendor's `set_th` inverse):**

* **v1, hwver 3 (this board):**
  ```
  raw = readl(base + 0x04) & 0x3ff;
  temp_mC = ((raw * 165 - 20625) / 797) * 1000 - 40000;
  ```
  i.e. T(°C) = (165·raw − 20625)/797 − 40, slope ≈ 0.207 °C/LSB, range ≈ −65 °C (raw 0) to
  +145 °C (raw 1023); integer division truncates toward zero. Cross-check: the vendor's
  `set_th` for hwver 3 computes the inverse `raw = (797·(T+40) + 20625)/165` (constants
  `0x31d=797`, `0x7c88=31880`, `+125` after the /165 step — algebraically identical). [V]
* v1 hwver 1/2 use the same `+0x04` 10-bit field with their own magic constants (not used on this
  board).
* **v4** (not this board's variant) reads/writes `+0x24`/`+0x10`/`+0x14` and converts
  `((raw*165 - 23925)/795)*1000 - 40000` (normal) or `((raw*165 - 32835)/605)*1000 - 40000`
  (alternate, selected by the high half of the hwver argument). **The `/100` formula in the current
  6.18 patch is not what the vendor code does** — see correction §6.2. [V]
* The temperature callback additionally adds the DT `trim` value (`+0x28`) when
  `efuse-trim-en` is set. [V]

### 3.3 Why our read of `0x14900500+0x24` returned 0, and why thermal is ENODATA today

The current 6.18 stack (patch `012`, `lab/luofu-thermal`) does:

```
raw = readl(base + 0x24) & 0xffff;
mC = ((165*raw - 32835) / 100) * 1000 - 40000;   /* decoded from v4, /100 */
```

On this board's DT (`hwver = 3`) the vendor selects the **v1** table, whose live temperature
register is **`+0x04` (10-bit)**, not `+0x24`; and the 6.18 driver performs **none** of the
vendor's probe-time enable/trim writes, so even the right register would not have been started.
Reading an un-driven `+0x24` returned 0 → the wrong formula extrapolates to −368,000 mC →
`<= THERMAL_TEMP_INVALID` → the thermal core returns `-ENODATA` (`/sys/class/thermal/.../temp:
No data available`). The block is therefore **[I] not dead** — barring an unobserved hardware fault, the wrong variant/register plus the missing enable sequence explains the reading; this must be confirmed by the bench steps in §3.4. The "vendor DT disables it" statement recorded on 2026-11-21 is a second error: the
vendor node is enabled, and the disabled property is ours. [V]/[V\*]

What we **do not** yet have: a live `+0x04` reading under either kernel, and a measurement of the
vendor's own thermal zone on the stock boot (read-only sysfs). Both are needed to confirm the
conversion's absolute offset/range and to tune trips. The raw store is still safe to read; the
enable/trim writes are the part that needs an attended bench session.

### 3.4 What a validated driver needs

1. **Correct variant path:** select v1 ops for `hwver == 3` (or model it as one of the three v1
   sub-formulas), read `base+0x04` low 10 bits, use
   `mC = ((165·raw − 20625)/797)·1000 − 40000`, add the DT trim only for `efuse-trim-en`.
2. **The enable/trim sequence from §3.2** (order, masks, 10-loop delay):
   `set_mode(base,0)`, `set_trim(base, efuse_trim_en, trim)`, `set_en(base,1,addition_en)`
   (which writes `+0x00.bit24` and, with `addition-en`, `+0x400.bit0`), then optionally threshold
   and interrupt. These writes are on the vendor's own boot path, so they are not new inventions —
   but they have not been executed by our kernel on this board.
3. **Register window:** declare/cover `0x14900500..0x14900908` (or split `+0x400`), keep the
   shared-page contract with pinctrl/LED-PWM/PMU-PWM in `0x14900000` and do not claim the whole
   page.
4. **Bench validation (attended):** first boot with the read-only conversion; compare
   `thermal_zone0/temp` with the vendor boot's sysfs value at the same idle/load point; then enable
   the writes and compare again. Recovery door armed, UART captured, no vendor module unload, no
   other block writes.
5. **Policy:** vendor trips 115 °C passive / 125 °C critical / 3 °C hysteresis and
   `polling-delay=1000` are vendor data, not ours. The cooling map targets `hsan,net-cdev`
   (network rate limiting via LSW CAR), which our 6.18 tree does not have; keep the zone with
   trips, drop or defer the cooling map (documented decision), and do not invent trip values.
6. **Optional:** hwmon exposure, IRQ/threshold path (SPI 0 threaded), critical-event work.

**Effort and risk:** ~2-4 host-side days (driver rework + binding/DTS correction + review) plus one
attended bench session for read validation and one for the writes; low-medium risk (touched block is
in the peripheral-control page, but the vendor sequence is known and the writes are confined to
`+0x00/+0x04/+0x08/+0x0c/+0x10/+0x400`; no CRG/reset writes, no NAND, no PCIe). **Verdict: telemetry
is probably achievable and the current [X] is a driver bug, not a hardware wall** — but it is not
"validated" until the bench steps in 4 are done, so §5 keeps it [X] for the shipped target.

---

## 4. Other unsupported / missed blocks in the vendor DT and rootfs

Scan method: decompile the vendor base DTB (§1.3) and enumerate every node/compatible/status;
enumerate the 58 `/lib/hisilicon/ko` modules and 198 `/lib/modules/5.10.201` modules; read every
`insmod`/`modprobe` in the vendor init scripts; compare with the 6.18 target skeleton. The task's
five named blocks:

| block | vendor DT | vendor rootfs/driver | our 6.18 target | verdict / effort |
| --- | --- | --- | --- | --- |
| **USB** | **no USB controller/PHY node anywhere.** Only the efuse nvmem cell `usb2_trim_offset` (`/efuse/ns/usb2_trim_offset`, alias `usb2_trim_offset`) and an unused `usb2` IDDQ comment | **no USB modules shipped**: `hi_usb.ko`, `hi_kusb2phy.ko`, `hi_kupsphy.ko`, `dwc3.ko`, `xhci*`, `ehci*`, `ohci*` are all absent; `/etc/init.d/hsan_prepare` has a **dead** USB branch that would insmod them if `hi_cfm` reported a USB count — they do not exist, so the path cannot execute | no USB nodes, no USB Kconfig/driver, `/sys/bus/usb` absent | **not a missed feature: the board has no USB.** No work item. (A USB-capable variant of the SoC would need the absent PHY/controller code = [X].) |
| **RTC** | **absent** (no `rtc` node/compatible anywhere) | no `rtc-*` module in `/lib/modules`; no RTC in userspace | no rtc class/driver/device; `/dev/rtc` absent | **absent by design**; no work item. |
| **I2C** | `i2c0@0x10111000`, `snps,designware-i2c`, `clock-frequency=100000`, clock `i2c0_clk`, reset `(0x2c,0x18)`, IRQ SPI 44 — node present and enabled, **no child devices** | `i2c-designware-core.ko` + `i2c-designware-platform.ko` exist in `/lib/modules` but are **never loaded** by any init script; no consumer | node present but `status="disabled"`; no driver/bus | **unused; optional ~1-2 host days** if a consumer ever appears (plain DW I2C). Not a target. |
| **PWM** | two separate blocks: `pmupwm: pwm@1` (`hsan,pmu-pwm`, reg `0x14900800`, `npwm=1`) + `reg@0` `pwm-regulator` `vdd_logic` (used for CPU voltage); `ledpwm0: ledpwm@14900280` (`hsan,ledpwm`) with `led_pwm` CRG gate | `hi_kpmupwm.ko` + `pwm-regulator.ko` loaded by `rc.d/luofu` for CPU `vdd_logic`; **no LED-PWM driver module shipped** and no consumer for `ledpwm` | no PMU-PWM/regulator in target DTS, no LED-PWM; not needed for wired target | **PMU-PWM is a CPU-regulator function, optional** (2-3 host days + bench if cpufreq/voltage control is wanted); **LED-PWM is dead** (no driver, no consumer) — do not port. |
| **NFC** | **absent** (no node, no compatible, no alias) | no NFC module, firmware, userspace or config | absent | **absent**; no work item. |

Additional "missed/unsupported" items found by the same scan:

| item | finding | consequence |
| --- | --- | --- |
| missing modules referenced by shipped init scripts | `hi_kteam.ko` (rc.d/luofu), `hi_kwificlk.ko` (`wifi_init.sh`), `hi_usb/hi_kusb2phy/hi_kupsphy` (`hsan_prepare`) are referenced but **not present** in `/lib/hisilicon/ko` (58 modules) | dead/variant code paths; do not treat script references as hardware evidence. In particular there is **no `hi_kwificlk.ko`** to port for Wi-Fi clocking. |
| `hi_knet_cdev.ko` ("hsan,net-cdev") | thermal cooling device: `devm_thermal_of_cooling_device_register`, `car-rate-table`, `dev-names`, `net-flow-car`; loaded immediately before `hi_tvsensor` | it is the cooling-map target of the vendor thermal zone; a 6.18 thermal port without the LSW CAR model should keep the zone and defer/drop the cooling map (§3.4) |
| `hi_woc_wifi_rtx.ko` + `hi_kcfe_lrn.ko` | loaded by stock before Wi-Fi; wifi.ko resolves 5 `hi_wifi_*` + 2 `hi_cfe_flush_*` via `__symbol_get` at runtime | part of the hard Wi-Fi closure; a 6.18 port must carry or replace them (§2.2) |
| `hi_khslink.ko`, `hi_swa.ko`, `hi_kipc.ko`, `hi_kmsgcenter.ko`, `hi_netif.ko`, `hi_xt_fwd.ko` | vendor app/HSLINK/RPC and bridge hooks | needed only by the vendor userland stack (hybrid); not part of a clean 6.18 target |
| `hi_kcapture.ko`, `hi_kdiagnose.ko`, `wifi_debug.ko`, `plat_debug.ko` | debug/DFT only (conditionally loaded / present but unused) | not production blocks |
| `hi_kjent_rng.ko` / `jent-rng` DT node | **software** jitterentropy RNG wrapper, not a hardware RNG | already recorded in MISC-SOC-BLOCKS; no hwrng to port |
| PCIe root complexes + Wi-Fi endpoints | both RC nodes enabled in vendor DT; our 6.18 target keeps them disabled and has a draft host driver (patch 013/014) that has never booted | the only Wi-Fi-related 6.18 workstream that does not need vendor source (§2.6) |
| SDIO/MMC, SATA, audio/codec, CAN, camera/ISP, GPU | **none** in the vendor DT or rootfs module set | not misses; do not plan for them |
| "EFS" | no EFS file system/container exists (§2.5) | use the flat fac-partition stores + INI; no EFS parser needed |

---

## 5. Effort summary and honest verdict per block

| block | verdict today | realistic route | effort | blocking dependency |
| --- | --- | --- | --- | --- |
| **Wi-Fi AP/client on 6.18** | **[X]** not reachable | vendor-source port (A) only; hybrid 5.10 (B2) as fallback | 10-16 eng-wks after source + bench | vendor GPL source; closed firmware/protocol; PCIe host bring-up; upstream impossible |
| Wi-Fi PCIe host (enabling work) | draft, never booted | validate patch 013/014 in attended run | 2-4 eng-wks | bench time/recovery |
| Wi-Fi clean-room full-MAC (no source) | **[X]** years; no partial AP/STA milestone shown | — | programme-level | closed firmware + undocumented H2D service |
| **Thermal telemetry** | **[X]** today (driver bug + missing enable); sensor works per decode | fix variant/register/formula + enable sequence, validate on bench | 2-4 host days + 1-2 attended sessions | bench access; no vendor source needed |
| USB | **absent** | none | 0 | not on board |
| RTC | **absent** | none | 0 | not on board |
| I2C | unused | optional DW-I2C enable if consumer | 1-2 days | none (no consumer) |
| PWM (PMU/CPU vdd) | vendor-only | optional pwm-regulator port | 2-3 days + bench | policy decision |
| PWM (LED) | dead | do not port | 0 | no driver/consumer |
| NFC | **absent** | none | 0 | not on board |

---

## 6. Corrections to existing docs (each with the evidence that overturns the earlier text)

1. **"The vendor devicetree marks the tvsensor disabled"** (FEATURE-INVENTORY UPDATE 2026-11-21 and
   the corresponding ledger entry). **False.** The vendor base DTB (re-extracted from
   `mtd11-kernela.bin` in this lane) has no `status` property on `tvsensor@14900500`; the board
   overlay has no thermal fragment; the vendor loads `hi_knet_cdev` then `hi_tvsensor`. The
   `status = "disabled"` lives in **our** reconstructed `docs/soc/luofu-r116.dts`. The correct
   statement is "the 6.18 driver's read path is wrong; the vendor block is enabled."
2. **The thermal conversion used by the 6.18 patch** (`+0x24 & 0xffff`,
   `((165·raw−32835)/100)·1000−40000`, "hwver 3 selects v4"). **Wrong on three counts:** for
   `hwver 3` the vendor selects the **v1** table; the live register is **`+0x04[9:0]`**, not
   `+0x24`; and the formula is `((165·raw−20625)/797)·1000−40000` (the v4 formulas the earlier
   doc tried to read are `/795` and `/605`, not `/100`). Evidence: Ghidra decompilation of
   `hi_tvsensor_hw_v4_drv_register`/probe variant gate and both `get_temp` functions in this
   lane; inverse cross-check in `set_th`.
3. **"No vendor code writes the `0x5a5a` boot magic; the vendor does not take that branch"**
   (`phase19/fw-boot.md` §A.0/A.4). **False.** In the 2.4.15 module the chunk loop falls through
   after the last chunk to the single-shot descriptor path and executes
   `bal_write(chip, 0x40000108, &0x5a5a, 4)`; the descriptor `{0x40000108, 0x5a5a, 4}` is at
   `.data+0x26a4` (verified). The vendor **does** kick the CPU; the phase-19b experiment had
   already proven the semantic independently.
4. **"0 mac80211 symbols"** (`PORT-SURFACE.md`, DRIVER-BLACKBOX). **Wrong by one:**
   `ieee80211_get_channel_khz` is imported (1 mac80211 symbol; 25 cfg80211/wiphy imports). The
   conclusion "not a mac80211 driver / full-MAC" is still correct.
5. **"211 namespaced symbols"** (`PORT-SURFACE.md`). 211 is the number of per-import
   `import_ns=HW_RTOS_NS` modinfo tags on wifi.ko (46 on plat.ko), not a unique-symbol count; the
   modules have 308/126 ordinary undefined imports and the tags do not create a link-time
   requirement for a mainline "namespace provider". (Already corrected at more length in
   `target-skeleton/docs/incoming/wifi-recon/REPORT.md` §3.2; restated here because this report is
   the scope baseline.)
6. **Thermal register window `0x14`** is not only too short for `+0x24` (old note): for the real
   v1 path it must cover `+0x00..+0x10` **and `+0x400`** (addition-enable at `0x14900900`), i.e.
   at least `0x408` bytes / a split resource, if the vendor enable sequence is implemented.
7. **"EFS"** in earlier shorthand: no EFS exists; the persistent store is the `fac` MTD5 mount
   `/usr/local/factory` with the `ZZZZ`-container blobs and INI pair (§2.5).

---

## 7. Evidence ledger (what a reviewer should be able to re-derive)

| claim | artifact / command | grade |
| --- | --- | --- |
| module sizes, md5/sha256, sections, symbol/import counts, license/vermagic | `readelf -hSW`, `readelf -p .modinfo`, `nm -S/-u`, `sha256sum` on `recovery-extract/lib/hisilicon/ko/*.ko` (2.4.15 set) | [V] |
| firmware/INI/KV hashes/sizes, header words, banner | `sha256sum`, `xxd -l 32`, `strings` on `/lib/firmware/hi_wifi/*` and the `cal-snapshots/25691117-005835` bundle | [V] |
| vendor DT tvsensor enabled + thermal zone/cooling map | FDT scan of `dumps/router-20261108/full/mtd11-kernela.bin` (base `0x418610`, overlays `0x420064`/`0x421254`), `dtc -I dtb -O dts` | [V] |
| vendor thermal driver variant selection/register map/formulas/enable sequence | Ghidra 12.2 headless + `objdump -dr` on `hi_tvsensor.ko`; addresses `0x24c/0x5e0/0x630/0x738/0x784/0x798/0x7a4/0x860/0x8b8/0x8d4/0x960/0x9dc/0xa78/0xaac` | [V] |
| firmware download/kick/IRQ/ready-message sequence | `objdump -dr` on `hi5622v100_plat.ko`: `firmware_download_function 0xf9f8`, `firmware_download 0xf834`, `firmware_file_send 0xf56c`, `bal_write 0x10c04`, `pcie_write 0xbdc0`, `multi_chip_loading 0xf2ac`, handlers `0xeb78/0xebd8`; `.data+0x26a4` dump | [V] |
| endpoint topology/BARs/IDs, stock boot sequence, ready messages, calibration load success | `build/uart-logs/20261107T191003Z-poweron-uboot.log` lines 619-654, 835-895, 1008-1019 | [V\*] |
| PCIe iATU/inbound-region decode and CA→BAR mapping | `opensource/docs/phase18/inbound-map.md` (§A.1-A.6; device-measured read-backs), `phase17/fw-download.md` (§A.4-A.6) | [V\*] |
| ring/node/channel maps | `phase42/THE-RING-MAP.md`, `phase43/THE-CHANNEL-MAP.md` | [V\*] |
| `.kv` container format, writer/reader, offsets | `phase8/kv-format.md` (disassembly of wifi.ko writer/verifier and plat.ko reader) + snapshot files re-read in this lane | [V]\*/[V] |
| H2D dispatcher ceiling / no data path in the takeover line | `phase22-27`, `phase49/README.md` (line 0x4C/SGI-bank state) | [V\*] |
| vendor module/script gap list (missing `hi_*` modules, no USB/RTC/NFC, I2C/PWM state) | `ls` of `/lib/hisilicon/ko` (58) and `/lib/modules/5.10.201` (198); `grep insmod` over `/etc`; vendor base DTB node scan; 6.18 target DTS/config | [V] |
| prior port plans and API-delta numbers (10-16 weeks, 11/16 exports) | `target-skeleton/docs/incoming/wifi-recon/REPORT.md` §5.2, `PORT-SURFACE.md`, `REMAINING-DRIVERS.md` | [V\*] |

**Redaction:** no MAC, serial, die-id, PIN, secret or per-unit calibration value is reproduced in
this report. The vendor blobs themselves are not included; only hashes/sizes/offsets. Anything that
would require a vendor binary to reproduce is marked [V]/[V\*] with the local artifact named.

**What this lane did NOT do:** no device contact; no write to any vendor artifact; no module load;
no attempt to build a driver; no commit of any binary/firmware/calibration blob. The effort numbers
are engineering estimates, not commitments, and the thermal validation steps are a plan, not a
result.

---

## 8. Bottom line

* **Wi-Fi:** fully understood as a vendor full-MAC stack over PCIe with a closed 928,920-byte
  firmware; the firmware-load interface (iATU/BAR setup → contiguous `memcpy_s` to CA `0x01240000`
  → `0x5a5a` CPU-release kick → IRQ enable → two HCC ready messages) is decoded and corroborated by
  the stock boot log. A 6.18 port needs vendor GPL source and 10-16 engineer-weeks after it; without
  source there is **no realistic partial AP/client path on 6.18** — only the vendor 5.10 hybrid works.
* **Thermal:** the vendor sensor is enabled and driven on stock; the current 6.18 ENODATA is caused by
  a wrong-variant/wrong-register/wrong-formula driver with no enable/trim sequence, not by the DT or
  hardware. A small correction (~2-4 host days + attended validation) can likely deliver validated
  telemetry.
* **Other blocks:** USB, RTC and NFC are absent by design; I2C exists but is unused; PWM splits into
  a used-but-optional PMU regulator and a dead LED-PWM block. No further missed hardware blocks were
  found in the vendor DT/rootfs scan.

---

## 9. W23 implementation draft (follow-up, 2026-11-21, host-only)

The follow-up lane implemented the thermal correction as release-line **W23** patch drafts. Nothing
here is hardware-validated; the drafts live only under
`docs/incoming/wifi-thermal-re/w23/` and are designed to be staged by the lead.

### 9.1 What the drafts are

| draft | sha256 | content |
| --- | --- | --- |
| `w23/0001-thermal-luofu-tvsensor-w23.patch` (549 lines) | `c2a4697559a22fe3c3867b58b85382f95c3dd2b04a987a1c698174696c3253cc` | release patch 012's Kconfig help update + full rewrite of `drivers/thermal/luofu_tvsensor.c`; applies **after** 012 |
| `w23/0002-dt-bindings-thermal-luofu-tvsensor-w23.patch` (114 lines) | `197bb74aad60a4d8caacc22dacc91399a8dc967c28d6b8a5ef98a4727ef7ee9c` | binding extension: 0x408 window and vendor properties; applies after 012 |
| `w23/0003-dts-luofu-r116-tvsensor-w23.patch` (24 lines) | `3e78b67a4f3dbac7bfcd9ceb7393da89b7339b0379a5d04c06b4c93a93132a38` | release-staging DTS diff (`target-skeleton/dts/luofu-r116.dts`): `reg = <0x14900500 0x408>` plus `hwver/trim/int-base-offset/addition-en/efuse-trim-en/hisilicon,threshold-mc`; keeps `status = "okay"` |

Design and integration details: [w23/W23-DESIGN.md](w23/W23-DESIGN.md),
[w23/RELEASE-INTEGRATION.md](w23/RELEASE-INTEGRATION.md). Patched-content hashes: driver
`8de7df6c…06917d`, binding `095ad395…1243d3`, patched DTS `3b8727f5…6e98f04`.

### 9.2 Driver behaviour (summary)

* Variant selection per §3.2: `hwver 3` → v1 table, `raw = readl(base+0x04) & 0x3ff`,
  `mC = ((165*raw − 20625)/797)*1000 − 40000`; all five decoded conversions are implemented
  (v1 hwver 1/2/3 plus v4 normal) with the v4 alternate documented as unreachable/U.
* Enable/trim sequence under the gate: `+0x00` bit24=1, `+0x04` bit27=0 + trim source
  (efuse-trim-en → bit20=0; else sign/magnitude in bits 19/10..18), `+0x400` bit0=1 when
  addition-en, `+0x10` bit0=1 (mask the threshold IRQ; W23 claims no IRQ), optional
  `hisilicon,threshold-mc` → `+0x08[9:0]` via the derived inverse
  `raw = (797·(T_mC/1000)+52505)/165`, and `+0x0c=1` clear. The vendor's 10-loop delay is replaced
  by `usleep_range(1000, 2000)` (units [U]).
* Gate: `module_param_cb(enable, …)` default 0, writable at
  `/sys/module/luofu_tvsensor/parameters/enable`; while off, `get_temp()` returns `-ENODATA` so an
  undriven raw 0 cannot be published as a legal −65 °C. Two read-only sysfs attrs (`raw`, `regs`)
  on the platform device give the bench the raw word and register dump.
* Window: DT `reg` widened to `0x14900500..0x14900908` (length 0x408) so `+0x400` is mapped.

### 9.3 Host validation already done (no device, no ARM build)

* Applied current release patch 012 then W23 0001+0002 to a freshly extracted pristine
  `linux-6.18.60`: clean apply, no fuzz/rejects.
* `make defconfig` + `COMPILE_TEST/THERMAL/THERMAL_OF/OF/LUOFU_TVSENSOR=y` +
  `make -j16 W=1 drivers/thermal/luofu_tvsensor.o`: **RC=0, zero warnings**.
* checkpatch: **0 errors**, one "missing commit description" warning per draft (expected for bare
  release-line patches).
* DTS draft compiled with `cpp`+`dtc` (tree headers, 0 warnings); base DTB 12,846 B (was 12,678 B).
* `dt_binding_check` on the updated binding: **PASS** (dtschema 2026.9).
* NOT done: release-line validator, full OpenWrt/ARM target build, any boot/runtime test.

### 9.4 Staging and bench test

The exact staging commands, manifest lines (suggested `066`/`067`), L0/L1 count updates (64→66,
53→55), truth-update list, and the complete attended bench procedure (gate-off baseline, gate-on
register readback, devmem cross-check, temp sanity vs room thermometer, load test, no-IRQ-storm
criteria, reboot check, abort/rollback) are in [w23/RELEASE-INTEGRATION.md](w23/RELEASE-INTEGRATION.md)
§2-§6. Do not run it unattended; the recovery card, UART capture and stock pair A are required.

### 9.5 Corrections found while writing the driver (in addition to §6)

8. **The release DTS was already enabled.** The brief said "the DTS node status that we currently
   have disabled", but `target-skeleton/dts/luofu-r116.dts` (the release staging DTS, and the
   2026-11-21 canonical image DTB) has `status = "okay"`. The disabled node is in the lab tree
   (`docs/soc/luofu-r116.dts`, `lab/luofu-kernel/dts/luofu-r116.dts`). The W23 DTS draft therefore
   keeps the node enabled and only adds the properties/window; if a staging base really has it
   disabled, set it okay in the same hunk.
9. **The threshold-mask polarity is inferred, not proven.** The vendor masks (+0x10 bit0=1) before
   programming and unmasks after installing its IRQ handler; W23 leaves it masked because it does not
   claim the IRQ. This is a bench abort criterion, not a fact.
10. **The 0x408 window overlaps the PMU-PWM word at 0x14900800.** That is harmless today (no PMU-PWM
    node in the release target) but must be split into two resources if PMU-PWM is ever enabled.
11. **The old "report the math as-is" comment is unsafe:** with the corrected formula, undriven
    raw 0 maps to −65 °C (a legal thermal value), hence the gate returns ENODATA instead.
12. **Module parameter name:** the runtime path is `/sys/module/luofu_tvsensor/parameters/enable`;
    the brief's `luofu_thermal=1` becomes `luofu_tvsensor.enable=1` as a boot token or the sysfs
    write above.

