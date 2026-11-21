# Hi5622V100 closed Wi-Fi firmware — external-interface black-box characterization

**Lane:** `wifi-firmware-re` (host-only, read-only). **Written:** 2026-11-21.
**Scope:** characterize the external interface of the closed on-device Hi5622V100 ("ShuangTaV100",
firmware banner `ChenTangV100R001C20T13`) radio firmware as far as a black box allows, and document
what a driver author must reproduce on the host. Companion: `SUMMARY.md` (one-page verdict).
**No device was touched**; no vendor binary, firmware blob, calibration value, MAC, serial or die-id is
committed here. Every offset is re-derivable from the named local artifact.

**Method / limits.** Local files only: the firmware blob
`/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 B, sha256
`7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b`), the two INI config files next
to it, the shipped driver modules (2.4.15 set, `hi5622v100_plat.ko` md5
`f688618b4daa52473be41577f992f7d4`, `hi5622v100_wifi.ko` md5 `1745ef77e728c411cef1921af3dc8731`),
the stock UART boot capture `build/uart-logs/20261107T191003Z-poweron-uboot.log`, and the project's own
prior host/device RE records (`opensource/docs/DRIVER-BLACKBOX.md`, `phase17-19/42/43/49`,
`docs/incoming/wifi-thermal-re/REPORT.md`). Fresh host-side work this lane: `xxd`/`strings`/`binwalk`,
entropy and region scans, and `arm-none-linux-gnueabihf-objdump` disassembly of the blob.

**Evidence markers** (project convention): **[V]** reproduced/verified by this lane from a named local
artifact (host-side, read-only); **[V\*]** measured on the device by an earlier attended lane and
recorded in the named repo doc (not re-run here); **[I]** inference; **[U]** unknown/unmeasured.

---

## 1. Binary layout

### 1.1 Container: monolithic, header + code + data, no ELF

The blob is a **single contiguous, uncompressed** ARM firmware image. There is no ELF header, no
compression container, no filesystem, no vendor signature block.

| property | value | evidence |
| --- | --- | --- |
| size | 928,920 B = `0xE2C98` | `stat` [V] |
| sha256 | `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b` | `sha256sum` [V] |
| `file`/binwalk | "data"; **0 signatures** (85 file signatures / 187 magic patterns) | `binwalk` [V] |
| max 4 KiB entropy | 7.256 bits/byte (code), 5.5–7.0 (data) → not compressed | per-block scan [V]; DRIVER-BLACKBOX §16 |
| ELF magic `0x7fELF` | **absent** | whole-file scan [V] |

### 1.2 Header (offset 0x00)

    00000000: 7169 0400 2d74 0c00 0000 0000 0000 0000   qi..-t..........
    00000010: ... (0x00-0x4f all zero) ...
    00000050: 4031 1000 1431 1000 e830 1000 bc30 1000   @1...1...0...0..
    00000060: 9030 1000 04e0 4ee2 1305 6df9 1300 02f1   .0....N...m.....

| offset | word (LE) | reading |
| --- | --- | --- |
| 0x00 | `0x00046971` | **entry pointer**, odd (bit 0 set) = Thumb → file offset 0x46970 |
| 0x04 | `0x000C742D` | **data-table pointer**, odd → file offset 0xC742C |
| 0x08–0x4F | 18 × 0 | zero padding |
| 0x50–0x63 | 5 × u32 | **exception vector table** (see 1.3) |
| 0x64 | — | first code byte (ARM mode) |

At file 0x46970 there is valid Thumb-2 code, and at file 0xC742C there is a byte table (see 1.5), so the
two header words are **file-relative** offsets (the same reading as DRIVER-BLACKBOX §16: "both odd, i.e.
Thumb pointers into the file"). [V]

### 1.3 The exception vector table (offset 0x50)

    0x50: 0x00103140   0x54: 0x00103114   0x58: 0x001030e8
    0x5c: 0x001030bc   0x60: 0x00103090

Five raw 32-bit handler addresses, monotonically descending by `0x2C` (44 B). They are **device
address-space (CA) values, not file offsets** — they exceed the file size. Under the region-0 alias map
(file 0 ↔ device CA `0x40000`, §3.1) they resolve to **file 0xC3090–0xC3140**, where five nearly
identical ARM-mode exception stubs live (each 0x2C B apart, content-matched):

    0xc3090: e49d0004  ldr r0,[sp],#4      ; 4 identical handler bodies (0xc3090/0xc30bc/0xc30e8/0xc3114)
    0xc3094: e24dd044  sub sp,sp,#0x44
    0xc3098: e88d1fff  pop {r0-r12}
    ...
    0xc3140: e49d0004  ldr r0,[sp],#4      ; 5th handler = reset/entry stub (matches the 0x64 body)
    0xc3144: e24ee004  sub lr,lr,#4
    0xc3148: f96d0513  srsdb sp!,#19
    0xc314c: f1020013  cps #19
    0xc3150: e92d1fff  push {r0-r12}

The `srsdb sp!,#19` / `cps #19` (switch to SVC mode) prologue identifies the core as an
**ARMv7 (ARM + Thumb-2)** CPU — the Wi-Fi chip's own embedded core, not the SoC's Cortex-A9 host. [V]

### 1.4 Region map (coarse)

| file range | entropy | content |
| --- | --- | --- |
| 0x0000–0x0053 | — | header + vector table |
| 0x0064–0xC81FF | 7.15–7.27 | dense code: ARM exception stubs at the low end, then Thumb-2 body; entry at 0x46970 |
| 0xC8200–0xC83FF | — | **zero gap** (0x200 B) |
| 0xC8400–0xCF9FF | 5.53–6.95 | string/debug region: names, states, command strings (1.6) |
| 0xCFA00–0xCFBFF | — | **zero gap** (0x200 B) |
| 0xCFC00–0xE2C97 | 6.95 | version tables, HCC thread/message tables, ARM veneers, MMIO register-init table, `smac_*` symbol table |

The image is **monolithic** in the file: the host transfers it as one contiguous byte stream (2 chunks of
0x80000 + remainder, §2.1). The "regions" named in the INIs (§1.7) are **device address-space concepts**
(ITCM / DTCM / custom segment metadata), not file segments. [V] transfer; [U] device-side relocation use.

### 1.5 Entry point and data table

Entry at **file 0x46970 (Thumb-2)**:

    46970: f003 6370  and.w  r3, r3, #0xf000000
    46974: f240 41f2  movw   r1, #0x4f2
    46978: 4318       orrs   r0, r3
    4697a: f7bc f955  bl     0x2c28
    4697e: e7d4       b.n    0x4692a

The "data table" at **file 0xC742C** is a run of small byte/word values (not code), consistent with a
lookup table:

    000c742c: 04ff ffff fe00 0000 0501 ff00 0300 0100  ................

[V]

### 1.6 String / debug / "assert" surface

The firmware carries its own **debug symbol table** — device-side C identifiers (this is the "large
smac/hcc symbol table" of DRIVER-BLACKBOX §16), readable at the offsets below:

| offset | content | role |
| --- | --- | --- |
| 0xC40E9 | `phy2g0..3`, `phy5g0..3`, `mac2g0..4`, `mac5g0..4`, `device`, `vap_tid`, `user`, `user_tid` | band/radio/device object names |
| 0xC4188–0xC4403 | `g_uart_lock`, `g_irq_controller_lock`, `g_st_ipc_mbx_lock`, `g_st_heartbeat_res.st_lock`, `g_hcc_timer_lock`, `g_hcc_timeout_lock`, `pst_sr->st_sr_lock`, `pst_dr->st_dr_lock`, `pcie_msg->pcie_msg_lock`, `st_memcpy_comm_res->st_memcpy_lock` | lock/object debug names (proves internal SR/DR + pcie_msg + mailbox + memcpy subsystems) |
| 0xC4396 / 0xC43DE / 0xC43A5 | `hcc_factory`, `pcie_thread`, `DEVICE EXCP_INFO` | HCC factory, PCIe thread, exception dump |
| 0xC44D2 | `INIT NORMAL VERIFY20M VERIFY40M INVALID ACTIVE DOZE AWAKE START SWITCH WAIT` | device power/state-machine states (incl. the `VERIFY20M/40M` test tokens) |
| 0xC4FAD | `SAFE OVER PA_OFF` | thermal protection states |
| 0xC485B | `NOINTF DETINTF PKADJ` | interface/adjust states |
| 0xC6D3D | `IDLE WORK SCAN` | radio states |
| 0xC6328 | `HEARTBEAT` | keepalive |
| 0xC47C9 | `ulsch_active_bsr_list_lock`, `g_ulsch_bsr_list_resource_lock`, `g_ulsch_twt_user_list_lock` | MAC scheduler (uplink-scheduler / TWT) debug names |
| 0xC4A0E–0xC4C9D | `sudden_good_delta_gdpt_ratio`, `descend_protocol_pre_thrd`, `tx_rate_aging_time`, `probe_reduction_strategy`, … | rate-adaptation algorithm tuning parameters |
| 0xE266C–0xE2B90 | `smac_coex_rx_abort_end_5g_isr`, `smac_rx_complete_5g_isr`, `smac_tx_complete_5g_isr`, `smac_tbtt_5g_isr`, `smac_backoff_timeout_5g_isr`, … | **device-side ISR table** (full `smac_*` name list) |
| 0xE2824–0xE29C8 | `smac_msg_proc_set_mu_edca_timer_req`, `smac_msg_proc_ac_suspend_req`, `smac_msg_proc_rx_ring_reset_req`, `smac_msg_proc_beacon_tx_suspend_req`, `smac_msg_proc_enable_trx_req`, `smac_msg_proc_disable_trx_req`, … | **device-side H2D message-handler table** (the firmware's command surface, §4.3) |

There is **no classical `__assert_fail`/panic string table**; the closest is `err_name` (0xC4FE0) and
`DEVICE EXCP_INFO` (0xC43A5). [V]

### 1.7 The INI "segment" metadata (device address-space, not file offsets)

| source | key | value | note |
| --- | --- | --- | --- |
| `cfg_hi5622v100_hisi.ini` `[HOST_WIFI_NORMAL]` | `firmware_itcm_src_addr/len` | `0xf009c` / `0xadd0` | SDK-authored |
| same | `firmware_dtcm_src_addr/len` | `0xfae6c` / `0x1098` | SDK-authored |
| same | `firmware_custom_src_addr/len` | `0x1b2800` / `0x10` | SDK-authored |
| `cfg_device_hisi.ini` `[DEV_WIFI_CUSTOM]` ("boundary") | `firmware_itcm_src_addr/len` | `0x117bc4` / `0xa968` | build-script-written |
| same | `firmware_dtcm_src_addr/len` | `0x12252c` / `0x76c` | build-script-written |
| same | `firmware_plat_custom_src_addr/len` | `0x1b2800` / `0x60` | build-script-written |
| same | `firmware_wifi_custom_src_addr/len` | `0x1b2860` / `0x9a8` | build-script-written |

All of these are **larger than the 0xE2C98-byte file** and are **chip address-space values, not file
offsets**. The host INI layer parses the `firmware_*` key strings (present in `plat.ko`), but the host
transfer does **not** segment the file (§2.1); their boot-time use is device-side relocation/segment
metadata and is **[U]** — not needed to reproduce the byte transfer. [V] (keys present in the INIs and
in `plat.ko` strings; cross-checked against wifi-thermal-re §2.3).

### 1.8 MMIO register-init table + `DEADBEEF` sentinel (end of file)

At the end of the file (after a run of ARM veneers `ldr pc,[pc,#4]` at 0xE2500) sits a
**register-init table** of `{u32 device-CA, u32 value, u32 0}` triplets (12 B each):

    0xe2524: 0x40034000 <- 0x3a     0xe2530: 0x40034014 <- 0x3a
    0xe253c: 0x40034028 <- 0x3a     0xe2548: 0x4003403c <- 0x3a
    0xe2554: 0x40035000 <- 0x3b     0xe2560: 0x40035014 <- 0x3b
    0xe256c: 0x40035028 <- 0x3b     0xe2578: 0x4003503c <- 0x3b
    0xe2584: 0x40107000 <- 0x3a
    0xe2590: (pad 0)                0xe2598: 0x0000000f   ; count word
    0xe259c: 0xdeadbeef              ; sentinel

The values 0x3A/0x3B are the firmware's **own** self-initialization writes to the region-3 IO block
(0x40034xxx / 0x40035xxx / 0x40107xxx) at boot. This is the one little-endian `DEADBEEF` of
DRIVER-BLACKBOX §16; its preceding word is `0x0F` (= 15), though only 9 triplets sit in the contiguous
run, so the exact count semantics is **[U]**. [V]

---

## 2. Boot handshake contract (what a host must reproduce)

### 2.1 Firmware transfer (decoded in phase 17/18, restated)

The host writes the **whole blob contiguously** to device CA **`0x01240000`** via
`firmware_file_send -> bal_write -> pcie_write -> memcpy_s(host-VA-of-CA, buf, n)`, in chunks of
`min(0x80000, remaining)` (this image: 524,288 B at CA 0x01240000, then 404,632 B at CA 0x012C0000).
There is **no ETE descriptor, no SR channel, no doorbell and no per-chunk header** in the data path —
the chunks are raw file bytes. The target CA falls in inbound region 5 (ACP, host BAR0+0x6b8000), so the
write lands at **BAR0+0x6f8000** (also aliased at **BAR0+0x40000** = device CA 0x40000, region 0). [V]

Prerequisites the host must program first (probe time, decoded phase 18): the six inbound iATU viewports
at `iatu_bar1 (BAR2) + 0x104 + 0x200*i` (ctrl2 = `(bar&7)<<8 | 0x80000000`, then base/limit/target),
then `PCI_COMMAND = 7`. [V\*]

### 2.2 The CPU-release write

After the last chunk the vendor executes exactly one device write (the single-shot descriptor at
`.data+0x26a4` = `{0x40000108, 0x00005a5a, 4}`):

    bal_write(chip, 0x40000108, &0x5a5a, 4)

i.e. **write `0x00005a5a` (4 bytes) to device CA `0x40000108`**. This is the chip-CPU release kick.
Phase 19b proved it semantically: after this write the firmware BSS zeroes, analog/status registers go
live, and the first mailbox word appears within ~3 s. [V\*] (wifi-thermal-re §2.4.3 corrected the older
"vendor never takes this branch" claim: it does, after the last chunk.)

Region-3 (IO) host base is BAR0+0x3b8000, so **CA 0x40000108 ↔ BAR0+0x3b8108**. [V]

### 2.3 The IRQ enable and the two ready messages

After the kick, the vendor's `wlan_power_on` calls `bal_irq_enable(0)` (host-side `enable_irq` only —
no device write), then `multi_chip_loading` waits on two completions completed by two **HCC messages**
(installed by `plat_init_bal_hcc_excp`, table `.data+0x2660`, 5 entries):

| # | chip-table id | handler | host completion | payload |
| --- | --- | --- | --- | --- |
| 1 | **1** | `device_plat_ready_msg_process` (log "Device plat ready! chip id : %d") | 200-jiffy wait | header only (source core 0) |
| 2 | **2** | `host_ready_msg_process` (log "DEVICE READY") | 2000-jiffy wait | `len-0xc` (≤0x62) bytes → `g_dmac_to_hmac_read_msg[core]` (0x64-stride) |

Wire header the handlers read: `byte[1]>>4` = source core, `u16@+4` = length, `byte[9]` = core index,
payload at `+0xc`. There is **no host→device message in the ready path** — the host only enables the IRQ
and reads status (`dev_status_check`, 11 read-only registers). [V]

### 2.4 The second control word: `0xcece` at CA 0x4000010c

The firmware's own D2H announce path (`d2h_notify` / `pcie_msg_init`, disassembled in
phase43 `device-dr-side.md`) spins until the host writes **`0xcece` to device CA `0x4000010c`**
before it publishes D2H notifications. So the host's boot control words are the adjacent pair:

| CA | host write | meaning |
| --- | --- | --- |
| **0x40000108** | `0x00005a5a` | CPU release (phase-19b-proven) |
| **0x4000010c** | `0x0000cece` | D2H announce-path gate (firmware-side precondition; the host peer that writes it is **[U]**/not-yet-pinned in the host object) |

**[V]** for the firmware-side `==0xcece` test (phase43); **[U]** for the exact host write site.

### 2.5 First observable responses (cross-referenced with the stock UART boot)

Stock boot (`build/uart-logs/20261107T191003Z-poweron-uboot.log`, lines 619–895):

    [11.48]  PCIe:0 switch to RC mode            [11.55] pci 0000:00:00.0: [59e7:0005] class 0x028000
    [11.59]  BAR 0 = 0x40000000-0x40ffffff 64bit  (16 MiB)
    [11.60]  BAR 4 = 0x41000000-0x417fffff 64bit  ( 8 MiB)
    [11.60]  BAR 2 = 0x41800000-0x41803fff 64bit  (16 KiB, iatu_bar1)
    [14.07]  enter firmware_download_function    [14.08] version check skip
    [14.08]  download firmware file buf len is [524288]
    [14.38]  finish download firmware            [14.38] firmware_download success
    [14.40]  [wlan_power_on]power on ... max time [320196]us
    [14.40]  Device plat ready! chip id : 0      <-- HCC ready message #1 (id 1)
    [14.40]  dev_status_check:: lock_status=0x6
             dev_status_check:: pcie0_status=0x2009a236  pcie1_status=0x2000a236
             dev_status_check:: dcoldo_efuse=0x0  dcoldo_vset=0x6
             dev_status_check:: tcxo_pll_mux_sel=0x1  tcxo_pll_status=0x2
             dev_status_check:: efuse_chip_id=0x186  pbank_code=0x312  abank_code=0x109  temp=0x24
    [15.22]  (second dev_status_check; abank_code advanced 0x109 -> 0x10a)  <-- ready message #2 (id 2)
    [15.29]  swa_netdevice_event ... name:Hisilicon0   <-- netdev created
    [15.30]  2g/5g the cali result to device succ      <-- calibration delivered

The 11 `dev_status_check` registers (and their CAs) are the **status-word table** the firmware exposes
after release:

| name | device CA | BAR0 (region-3 base 0x3b8000) | observed stock value |
| --- | --- | --- | --- |
| `efuse_chip_id` | 0x400002A8 | 0x3b82a8 | 0x186 |
| `dcoldo_efuse` | 0x400002D4 | 0x3b82d4 | 0x0 |
| `dcoldo_vset` | 0x4000500C | 0x3bd00c | 0x6 (post-release 0x260d4184) |
| `pbank_code` | 0x4000505C | 0x3bd05c | 0x312 |
| `abank_code` | 0x40005060 | 0x3bd060 | 0x109 (free-running) |
| `temp` | 0x4003C110 | 0x3fc110 | 0x24 |
| `lock_status` | 0x4003C208 | 0x3fc208 | 0x6 |
| `pcie0_status` | 0x40039224 | 0x3f1224 | 0x2009a236 |
| `pcie0 latch` | 0x40039220 | 0x3f1220 | — |
| `tcxo_pll_mux_sel` | 0x40101230 | 0x4b9230 | 0x1 |
| `tcxo_pll_status` | 0x40101234 | 0x4b9234 | 0x2 |

**[V\*]** (phase19 fw-boot; stock log). The first mailbox word the released firmware writes is
**`out[1]` CA 0x40039014 = 0x4** (bit 2) at ~1.85 s after the 0x5a5a kick (phase19c fw-handshake),
decoded as "message id 2 pending" under the one-bit-per-id bitmap convention (§3.3). [V\*]

---

## 3. Shared-memory / mailbox / ETE layout

### 3.1 The six inbound regions (BAR0 decode)

The endpoint's BAR0 (16 MiB) is split into six named inbound regions (`g_shuangta_region_types`,
`.data+0x29d8`, 6 × 0x50):

| idx | device CA range | host BAR0 window | /proc/iomem name | use |
| --- | --- | --- | --- | --- |
| 0 | 0x00000000–0x001bffff | 0x0000000–0x01bffff | ROM_WRAM | ROM/RAM code + **firmware alias** at +0x40000 |
| 1 | 0x00400000–0x00417fff | 0x01c0000–0x01d7fff | TCM_NOACP | device TCM |
| 2 | 0x01000000–0x011dffff | 0x01d8000–0x03b7fff | PKTRAM_NOACP | packet RAM |
| 3 | 0x40000000–0x4011ffff | 0x03b8000–0x04d7fff | **IO** | register/IO block (ETE, mailbox, MAC regs) |
| 4 | 0x02000000–0x021dffff | 0x04d8000–0x06b7fff | ACP | ACP SRAM |
| 5 | 0x01200000–0x01417fff | 0x06b8000–0x08cffff | ACP | **firmware target** (CA 0x01240000) |

Region-3 rule for any IO CA: **BAR0 offset = 0x3b8000 + (CA − 0x40000000)**. [V]

### 3.2 ETE ring engine (block CA 0x4003a000)

The ETE block is **device CA 0x4003a000** = BAR0+0x3f2000 (under the vendor region map). It is a
register file of **7 channels × 0x50 bytes** — not 12:

| family | channels | block offsets | direction |
| --- | --- | --- | --- |
| SR0–SR2 | 3 | +0x400, +0x450, +0x4a0 | host → device |
| DR0–DR3 | 4 | +0x590, +0x5e0, +0x630, +0x680 | device → host |

Per-channel register map (both families):

| offset | field |
| --- | --- |
| +0x00 | per-channel **enable** (bit 0) |
| +0x08 | control nibble |
| +0x10/+0x14/+0x18/+0x1c | program group A: base / depth−1 / producer / consumer |
| +0x28 | resource word |
| +0x30/+0x34/+0x38/+0x3c | program group B: base / depth−1 / producer / consumer |
| +0x48 | second enable |

The host programs the host-DRAM group (SR → +0x10; DR → +0x30). **Ring nodes are 8 bytes**:
`word0 = device VA of buffer`, `word1 = (len<<16) | flags`, flags `0x6d2b`
(= `0xd2b | 0x2000 | 0x4000`). The 20-word channel array lives at `wifi.ko .rodata+0xb60` (band 0 =
2g, band 1 = 5g; the real MAC/ring register block is slot 5 = CA 0x40040000 / 0x40060000). [V\*]
(phase42/43 maps, device-measured.)

### 3.3 Mailbox / doorbells (six message registers, `shuangta_pcie_msg_reg_map`)

| slot | device CA | BAR0 | role |
| --- | --- | --- | --- |
| out[0] | **0x40039010** | 0x3f1010 | **H2D pending/message mask** (`pcie_msg_send` writes; `pcie_msg_init` zeroes) |
| out[1] | 0x40039014 | 0x3f1014 | message register 1 — **the firmware's first ready word (0x4)** |
| out[2] | **0x400392d4** | 0x3f12d4 | **doorbell** (`pcie_msg_send` ORs bit 0) |
| out[3] | 0x40101438 | 0x4b9438 | MAC-side message register |
| out[4] | 0x40101414 | 0x4b9414 | MAC-side message register |
| out[5] | 0x400392f0 | 0x3f12f0 | message register 5 (**never write** — project hard rule) |

Convention: **bit N of a message register = message id N** (`pcie_msg_handle` dispatches the lowest set
bit; `pcie_msg_send_irq` writes 8 = bit 3 = id 3 = `pcie_ete_transfer_done_handle`). The D2H doorbell
is a separate CA **0x40101434** (bit 0). [V]

### 3.4 Device-side preconditions for D2H deposit (phase43)

| # | precondition | evidence |
| --- | --- | --- |
| P1 | DR channel +0x00 enable = 1 (firmware sets it itself) | firmware `0x86110`/`0x09490` |
| P2 | `out[1]` (0x40039014) must read 0 before the next word (host must drain) | firmware `0x86120` |
| P3 | notify id ≤ 10 | firmware `0x86170 cmp r1,#0xa` |
| P4 | **CA 0x4000010c == 0xcece** (host-written magic) | firmware `0x86f74` |

**[V]** (phase43 device-dr-side). P4 is the same 0xcece gate of §2.4.

---

## 4. Message / command surface

### 4.1 HCC message service

The firmware runs three HCC threads, named in its own data:
`HCC TX Thread` (0xCCFC8), `HCC RX Thread` (0xCCFF8), `HCC OAM Thread` (0xCD028). The chip-level
handler table has ids **1** ("device plat ready") and **2** ("host/DEVICE READY"), plus PCIe-level ids 1
(dev-ready stub), 3 (ETE transfer done), 6/7 (trigger ETE sending). [V]

### 4.2 Host→device wire path

Host sends: `hcc_queue_tx_process -> bal_port_start_xfer -> pcie_xfer_data -> pcie_tx_request_handle ->
shuangta_ete_sr_dscr_fill` (SR node `word1 = len<<16 | 0x6d2b`) → `pcie_msg_send(chip,3)` → write
pending mask to 0x40039010 and OR bit 0 into doorbell 0x400392d4. [V]

### 4.3 Readable command/response tables

**Device-side H2D message-handler names** (the firmware's own `smac_msg_proc_*` table, readable in the
blob at 0xE2824–0xE29C8):

    smac_msg_proc_set_mu_edca_timer_req        smac_msg_proc_ac_suspend_req
    smac_msg_proc_set_cali_self_cts_tx_num_req smac_msg_proc_set_cali_ppdu_tx_num_req
    smac_msg_proc_rx_ring_reset_req            smac_msg_proc_update_tx_bypass_ctrl_req
    smac_msg_proc_beacon_tx_resume_req         smac_msg_proc_beacon_tx_suspend_req
    smac_msg_proc_clr_hw_fifo_req              smac_msg_proc_tx_resume_req
    smac_msg_proc_tx_suspend_req               smac_msg_proc_enable_trx_req
    smac_msg_proc_disable_trx_req

plus the full `smac_*_isr` interrupt-service table (0xE266C–0xE2B90) and the calibration/DFS command
names (`set_radar_th`, `get_radar_th`, `set_next_chan`, `dfsenable`, `cacenable`,
`set_5g_channel_bitmap`, `set_multiap_ie`/`get_multiap_ie`, …). [V]

**Host-side `iwpriv` / alg command surface** (decoded in DRIVER-BLACKBOX §12–14, cross-referenced here):
the `alg` (0x0101) and unregistered `cali` (0x0102) dispatch chains, with
`g_ast_alg_cfg_process_info_table` = 414 entries (189 getters / 225 setters, 177 get/set pairs).
Firmware-boundary handlers: `hmac_sync_dmac_alg_cfg_rsp_entry`, `hmac_sync_dmac_cali_cfg_rsp_entry`,
`hmac_chan_tx_cali_sync`. [V\*]

### 4.4 Status-code / version / feature strings

| class | strings | offset |
| --- | --- | --- |
| firmware version banner | `ChenTangV100R001C20T13` | 0xCD2EC |
| board build (INI header) | `2g_ipa_5g_ipa_AX3000_0615-oversea_CHENTANG-normal for V600_Cudy-WR3000v2_20250905` | `cfg_hi5622v100_hisi.ini` line 1 |
| silicon id | chip id `0x34` rev `0x00`; `efuse_chip_id=0x186` | `iwpriv get_chipid` / dev_status_check |
| power states | `INIT NORMAL VERIFY20M VERIFY40M INVALID ACTIVE DOZE AWAKE START SWITCH WAIT` | 0xC44D2 |
| thermal states | `SAFE OVER PA_OFF` | 0xC4FAD |
| radio states | `IDLE WORK SCAN`; `NOINTF DETINTF PKADJ` | 0xC6D3D / 0xC485B |
| regdomain countries | 2-letter codes `AR BR CA CL IN MX PR US AE … VN` | 0xC4420–0xC44CF |
| feature flags | `feature_flags=0xc03` (INI); `country_code=99`; `host_input_tcxo_freq=38400000` | INI |

[V] offsets; [V\*] device-measured id/status.

---

## 5. Honest verdict

**The Hi5622V100 firmware is a closed on-device program and cannot be clean-room reimplemented.** This
lane, and the project's prior lanes, decoded the *transport and the handshake* — the iATU region map, the
contiguous `memcpy_s` firmware load to CA 0x01240000, the 0x5a5a CPU release, the 0xcece D2H gate, the
two HCC ready messages, the mailbox/doorbell CAs, and the ETE SR/DR ring layout. What was **not**, and
cannot be, recovered by black-box means is the firmware's *behavior*: the MAC/MLME/rate/DFS/calibration
logic inside the 928,920-byte image is vendor C running on the chip's own ARMv7 core, communicating over
an undocumented H2D/D2H message protocol. The device-side `smac_msg_proc_*` names are readable (they are
debug symbols), but their wire encoding, ordering, state machines and timing are not — the earlier
from-scratch takeover line never reached a working data path (the H2D dispatcher gate, "line 0x4C / SGI
bank", remains closed: phase22–27, phase49). No partial AP/client milestone has ever been shown without
the vendor module stack.

**The only path to a working driver is host-side GPL source plus this interface contract.** The two vendor
modules declare `license=GPL` and are un-stripped with full symbol tables, so a source request is the
correct instrument; a 6.18 port is 10–16 engineer-weeks *after* the source arrives, and is
**upstream-impossible** regardless (closed firmware + vendor userspace ABI). This contract serves as the
cross-check list against whatever source is produced.

**Exact interface facts a GPL source request should cross-check:**

1. Firmware blob = 928,920 B, sha256 `7fc87e20…`, loaded **contiguously** to CA `0x01240000` (BAR0+0x6f8000), chunks of 0x80000, no headers/doorbells in the data path.
2. Six inbound iATU regions (`g_shuangta_region_types`); `PCI_COMMAND=7`; BAR0 16 MiB / BAR4 8 MiB / BAR2 16 KiB (`iatu_bar1`).
3. CPU release: `0x5a5a` → CA `0x40000108`; D2H gate: `0xcece` → CA `0x4000010c`.
4. Two HCC ready messages: id 1 ("Device plat ready!") then id 2 ("DEVICE READY"), waits 200/2000 jiffies.
5. Mailbox: H2D mask `0x40039010`, ready word `0x40039014`, doorbell `0x400392d4`, MAC-side `0x40101414`/`0x40101438`; D2H doorbell `0x40101434`; **never** write `0x400392f0`.
6. ETE block `0x4003a000`, 7 channels × 0x50 (SR0–2 @ +0x400/450/4a0, DR0–3 @ +0x590/5e0/630/680); node = 8 B `{devva, (len<<16)|0x6d2b}`; DR per-channel enable +0x00 and group B +0x30.
7. `dev_status_check` 11-register status-word table (CAs in §2.5).
8. Firmware header: entry `0x46971` (Thumb @0x46970), data table `0xC742D`, 5-vector table @0x50 → exception stubs @0xC3090.
9. The `smac_msg_proc_*` / `smac_*_isr` H2D handler names and the `alg`/cali dispatch (414-entry table) — these must match the source's message-id assignments.

**Redaction / scope:** no firmware or calibration blob, no MAC/serial/die-id/PIN, no per-unit value is
committed. All facts are re-derivable from the named local artifacts. This lane performed no device
contact, no module load, no build, and no write to any vendor artifact.
