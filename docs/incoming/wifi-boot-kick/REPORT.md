# Vendor boot-kick RE: what makes the Hi5622V100 firmware init table run?

Host-only reversal lane (boot-kick-re), 2026-11-23. Inputs are local vendor artifacts only;
no device contact, no vendor bytes committed. All offsets are **file offsets inside the named
module's sections** unless prefixed `.text.unlikely`; device addresses are "CA".

Evidence markers: **[V]** re-verified here from the named local artifact (objdump/readelf/hexdump);
**[V\*]** device-measured earlier and recorded in the cited lane doc; **[I]** inference; **[U]** unknown.
Prior lanes' verified results are cited, not repeated.

Sources used (local, untracked):
- `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko` (2.5.24, 364,660 B, not stripped)
- `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko` (2.5.24, 3,564,728 B)
- `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_{plat,wifi}.ko` (2.4.15)
- `plat_debug.ko`, `wifi_debug.ko` (2.5.24)
- firmware blob `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`
  (928,920 B; **never committed/copied**)
- our transport: `opensource/docs/incoming/wifi-transport-port/build/` (pciet_main.c, pciet_ete.c,
  hi5622_pciet.h) and B3 instrument `/tmp/b3-build/pciet_b3.c`
- prior lanes: `wifi-firmware-re/REPORT.md` §1.8/§2.1-2.5, `pcie-seq/vendor-pcie-sequence.md` §6,
  `wifi-hcc-re/REPORT.md` §4.4/§5.1, `wifi-fw-ready-gate/REPORT.md` §10-§14.

---

## 0. TL;DR — the single most likely missing host action

**The host must serve the firmware's `0xcece` gate *after* the release, while the firmware is
spinning on it — not once, immediately after the `0x5a5a` release.**

Verified chain:

1. The firmware's per-vector entry routine (file 0x86E14, CPU 0xC6E14) **zeroes**
   CA 0x4000010C with a 16-bit store at file **0x86E7E**, and also zeroes the release
   register CA 0x40000108 at file **0x86E80** [V].
2. It then ORs `1<<vector` into the halfword at CPU **0x40008** (the vector-seen mask, file
   0x86E96) [V].
3. It writes **out[1] = 4** (CA 0x40039014, file 0x86F5A) and the D2H doorbell (CA 0x40101434 = 1,
   file 0x86F66) [V].
4. It then **spins on the 32-bit word CA 0x4000010C == 0x0000cece** (file 0x86F78) [V].
   Three `movw rX,#0xcece` sites exist in the image (file 0x82102, 0x86F74, 0xD8EC4) and **all**
   are compare loops — the image never writes the gate [V].
5. The device's own boot-task prelude also writes 0xDEAF to CA 0x4000010C (file 0x7CEC) [V\*].

Our transport writes 0xcece **right after** release (B3-PLAN step 6), i.e. ~1.8-2 s before the
firmware reaches file 0x86E7E and wipes it. The cold-boot snapshot (fw-ready-gate §14) is exactly
this death: mask 0x08 = only entry 3 entered, out[1]=0x4 = entry 3 is parked at 0x86F78,
g(CA 0x172130)=0 = no later stage ran. The fix is not a new register — it is **holding the gate
value live and re-serving it for every vector entry** (mask 0x08 → 0x1E), ideally triggered by
polling out[1]/the mask, or simply by a sustained write loop.

Neither 2.5.24 nor 2.4.15 host modules contain the constant 0xcece in any form (see §2.4), so we
cannot copy a "vendor write site": we must reproduce the **effect** (gate reads 0xcece exactly when
each of the four entries checks it). The vendor peer that does this is outside `plat.ko`/`wifi.ko` [U].

---

## 1. Reconstructed vendor boot sequence (device writes/reads with offsets)

All entries are `plat.ko` 2.5.24. `bal_write(chip, CA, host_ptr, len)` is the vendor descriptor write
primitive: `bal_write` `.text 0x10c04` → `pcie_para_check` `.text 0xbc04` (converts CA to mapped
host VA) → `pcie_write` `.text 0xbdc0` → `memcpy_s`. [V]

### 1.1 PCIe/chip init at module load — `pcie_main_init` (.text.unlikely 0x704)

| # | action | offsets | evidence |
| --- | --- | --- | --- |
| 1 | `pcie_init_default` static resource init, software only | 0x768 | [V] |
| 2 | `pcie_bus_getnum`; `__pci_register_driver`; wait until per-chip probe counter `[pcie_res+0x6c] > 0` | 0x790-0x830 | [V] |
| 3 | `get_pcie_res`; `pci_dev_res_init` (ioremap/BAR tables) | 0x830-0x8b8 | [V] |
| 4 | **conditional** (probe counter > 1): `oal_pcie_enable_remap.part.0` (0x6a94) → if flag-file absent calls ops->+0x10 = **`shuangta_pcie_enable_remap` (0x1ae3c)** → **write CA 0x4003A200 = 1 (4 B)** after `dsb`+`arm_heavy_mb` (offsets 0x1ae6c-0x1ae7c) | 0x8b8-0x8e0 | [V] |
| 5 | `pcie_ete_init` (0x7820) — ETE ring/channel init writes into the CA 0x4003A000 block; intr block CA 0x40039508 | 0x8e8 | [V] + wifi-hcc-re §4.4 |
| 6 | `pcie_comm_init` (0x171cc) → `pcie_thread_init` (0x170b8) + `pcie_msg_init` (0xb6e4): `kmem_cache_alloc_trace` + `memset_s` + **4× `pcie_msg_register` — zero device writes** | 0x944 | [V] (call census + store census of 0xb6e4) |
| 7 | ops->+0xc = **`shuangta_pcie_pm_init` (0x1b57c)** → `pcie_l1ss_dev_pinmux_set` (0x1b440): **CA 0x40000554 ← (old \| 0x220), 4 B** (read-modify-write, dsb+arm_heavy_mb, offsets 0x1b468-0x1b490); **CA 0x400005BC ← 6, 4 B** (offsets 0x1b4a4-0x1b4c0) | 0x958-0x960 | [V] |
| 8 | optional board/debug workaround (only if `filp_open(LC75)` succeeds): convert CA 0x40002210, **RMW: value = (old & 0x3f) \| 0x180, 4 B** after `dsb`+`arm_heavy_mb` | 0x990-0x9d4 | [V]; file path meaning [U] |

The chip-ops table lives in `.data` at **0x29b8**: +0x00 region-types, +0x08 msg-reg map,
+0x0c pm_init (0x29c4), +0x10 enable_remap (0x29c8), +0x14/0x18 L1SS set/clear. [V, .rel.data]

### 1.2 Endpoint probe — `oal_pcie_probe` 0x4d0 / `oal_pcie_host_init` 0xbefc / `oal_pcie_dev_init` 0xa090

| # | action | offsets | evidence |
| --- | --- | --- | --- |
| 1 | `pci_enable_device` → endpoint PCI_COMMAND IO\|MEM\|BME (0x0007) | probe path | [V\*] pcie-seq §6 |
| 2 | `pci_read_config_byte` class/rev check; `pci_read_config_dword(pdev, 0xff8)` → PHY id (RC0 0x11521, RC1 0x11520) | host_init | [V\*] pcie-seq §6 |
| 3 | ioremap endpoint BAR0/BAR2 windows; `oal_pcie_enable_regions`; `oal_pcie_inbound_ca_to_va`; `memcpy_s` | 0xa090-0xad64 | [V] |
| 4 | `oal_pcie_set_inbound_by_viewport` (0x97a8) → `pcie_inbound_region_cfg` (0x94e0): endpoint iATU via **PCI-config dwords** `0x90c base_lo, 0x910 base_hi, 0x914 limit, 0x918 target_lo, 0x91c target_hi` (+ index/ctrl2 via the 0x97a8 wrapper), for the runtime region table built from `g_shuangta_region_types` (`.data 0x29d8`, 6×0x50) | 0x94e0-0x989c | [V]; [V\*] pcie-seq §6 |
| 5 | second `pci_write_config_word(endpoint, 0x04, 0x0007)` after window programming | region path | [V\*] pcie-seq §6 |
| 6 | `oal_pcie_probe_irq_init` (0x698) — request_irq only | 0x698 | [V] |
| 7 | `shuangta_pcie_msg_reg_map` (0x1b1a0) maps exactly **six** device CAs to host VA (see §2.5); read-only mapping, no writes | 0x1b1a0-0x1b438 | [V] |

`.data` firmware/region contract words (file 0x345f0-0x34620): per-chip table
`LANCHOR1 = .data+0x269c`: chip0 entry `{path_ptr, CA 0x01240000}`; per-chip release descriptor at
`LANCHOR1+8+12*chip` = `.data+0x26a4` = **{CA 0x40000108, value 0x00005a5a, len 4}** [V, readelf
relocs + hexdump; same descriptor in 2.4.15 at file 0x34604].

### 1.3 Chip power-on / firmware download — `wlan_power_on` 0xe46c → `firmware_download` 0xf834

| # | action | offsets | evidence |
| --- | --- | --- | --- |
| 1 | `plat_pm_get_drv`; `wlan_bal_init_process` (0xe308); first time `plat_init_bal_hcc_excp` (0xe380) registers HCC handlers (software only) | 0xe478-0xe504 | [V] |
| 2 | `hwifi_rf_cali_file_load` (0x3000) — calibration file to host memory, no device write | 0xe4b0 | [V] |
| 3 | `firmware_download_function` (0xf9f8): builds per-chip descriptor `LANCHOR0+0x20*chip`, path array and release descriptor pointers; prints "enter firmware_download_function" | 0xf9f8-0xfc18 | [V] |
| 4 | `firmware_download` (0xf834): `firmware_check_version` (0xf404); alloc 0x80000 bounce; loop `firmware_file_send` (0xf56c) per chunk | 0xf834-0xf9f8 | [V] |
| 5 | per chunk: `filp_open`+seek+`firmware_kernel_read` (0xf50c), then `bal_write(chip, CA_base+offset, bounce, len)` at 0xf724; CA_base = 0x01240000 (chip0), `len = min(0x80000, remaining)` → this image: 2 writes (0x80000 then 0x2C298) | 0xf56c-0xf76c | [V]; [V\*] wifi-firmware-re §2.1 |
| 6 | after the chunk loop: **single-shot release** `bal_write(chip, 0x40000108, 0x00005a5a, 4)` at 0xf8a0-0xf8b4 (descriptor `.data+0x26a4`) | 0xf8a0-0xf8b4 | [V] |
| 7 | back in `wlan_power_on`: `bal_irq_enable(0)` (0xe5f8, host `enable_irq` only — **no device write**) and return | 0xe5e8-0xe614 | [V] |
| 8 | `multi_chip_loading` (0xf2ac) waits on two completions: plat-ready 200 jiffies, device-ready 2000 jiffies (completed by the two HCC ready messages) | 0xf324-0xf3c8 | [V] |

Reads in this path: `dev_status_check` (0xf18c) reads the 11 CAs listed in `.rodata`
(0x400002A8, 0x400002D4, 0x4000500C, 0x4000505C, 0x40005060, 0x4003C110, 0x4003C208, 0x40039224,
0x40039220, 0x40101230, 0x40101234) [V]. No other device writes occur after the release [V].

### 1.4 What the firmware does with those writes (context, from the image)

- Ready-routine start (file 0x86E14): **strh 0 → CA 0x4000010C (0x86E7E)** and **strh 0 →
  CA 0x40000108 (0x86E80)** [V]; mask OR at CPU 0x40008 (0x86E96) [V]; frame staging to
  CPU 0x4000C..0x4004C [V]; helper pair writes 0x40039000/0x40039800 blocks [V];
  **out[1]=4 (0x86F5A)**; **CA 0x40101434 ← 1 (0x86F66)**; **spin CA 0x4000010C == 0xcece
  (0x86F78)** [V].
- Boot-task prelude writes 0xDEAF to CA 0x4000010C (file 0x7CEC) [V\*].

**Net timing rule:** any 0xcece written before 0x86E7E executes is destroyed; the value must be
present while 0x86F78 spins, and again for each later vector entry (each entry re-zeroes it).


### 1.5 Entry-routine literal map (file 0x86F88-0x86FC4, exact [V])

| literal @file | value | use in routine (file offset) |
| --- | --- | --- |
| 0x86F88 | 0x0017D398 | dispatcher/per-chip object base r5; flag word at r5+0xBE0 = 0x17DF78 (state bits 1/2/4/8 written at 0x86E42/0x86E8C/0x86F2C/0x86F70) |
| 0x86F8C | 0x00105FDC | per-core r0 = base + core*8, +4 loaded, call 0xC6DB8 |
| 0x86F90 | 0x40101250 | read, bit5 cleared, write (0x86E36-0x86E3E) |
| 0x86F94 | 0x40161F00 | per-core word written 0x20002 (or nothing for core 1) at 0x86E54/0x86E6C |
| 0x86F9C | 0x4003022C | word 0 written at 0x86E7A (the "live IO word" of fw-ready-gate Q2) |
| 0x86FA0 | **0x4000010C** | gate: halfword 0 written at 0x86E7E; spin-compare of word at 0x86F78 |
| 0x86FA4 | **0x00040008** | vector-seen mask halfword; OR (1<<vector) at 0x86E96; frame staged at +4.. (0x40000C..0x40004C) |
| 0x86FB0 | 0x40000558 | halfword bit0 cleared at 0x86F30-0x86F3C |
| 0x86FB4 | 0x40039000 | helper pair block A (call 0x407C0/0x4078E at 0x86F3E-0x86F50) |
| 0x86FB8 | 0x40039800 | helper pair block B |
| 0x86FBC | **0x40039014** | out[1] = 4 written at 0x86F5A |
| 0x86FC0 | 0x0017DF44 | lock object passed to 0xFDF72 after the spin (0x86F82) |

The release register CA 0x40000108 is zeroed at 0x86E80 via `strh r3,[r2,#-4]` with r2 = 0x4000010C.
Every later vector entry repeats the same zero+announce+spin, which is why the host must keep
serving the gate until the mask reads 0x001E.

---

## 2. Answers to the specific questions

### 2.1 Does the vendor write the firmware tail init table ({CA,value,len=0} triplets) before/after release?

**No — not from the host driver.** [V negative, exhaustive constant census]
- The tail table sits at file 0xE252C: 9 triplets `{0x40034000<-0x3a, 0x40034014<-0x3a,
  0x40034028<-0x3a, 0x4003403c<-0x3a, 0x40035000<-0x3b, 0x40035014<-0x3b, 0x40035028<-0x3b,
  0x4003503c<-0x3b, 0x40107000<-0x3a}`, then `0x0000000F`, then `0xDEADBEEF` [V, re-dumped].
- Neither `plat.ko` nor `wifi.ko` (2.5.24 **and** 2.4.15) contains the 32-bit constants
  0x40034000 / 0x40035000 / 0x40107000, nor a triplet table, nor `DEADBEEF` [V].
- Those three addresses occur only in the *debug* modules' `oal_pcie_print_bits` register-dump list
  (`plat_debug.ko` file 0x98f0 area, `wifi_debug.ko` 0x28410 area) — a name/address table, not a
  write list [V].
- Who applies the table: ROM/firmware-side code, or a host component outside these modules [U].
  The image contains no in-image reference to the table's CPU address (no 0x00102528/0x0010252C/
  0x00102530 literal) [V negative]; the table's values are region-3 IO words that the firmware's
  own IO init touches at boot [I]. Supporting detail: the image itself contains several other
  tables of the same `{CA, value, 0}` shape in its data region (e.g. file 0xCE700:
  `{0x40119028,0x42,0}`, `{0x4011903C,0x42,0}`, `{0x40107000,0x40,0}`) [V], so the format is the
  device's own register-init convention, not a host downloader descriptor list — which is why the
  host driver has no code for it.

### 2.2 Does it release more than one core/chip (multi_chip_loading)?

- `wlan_power_on` is **per chip**: `wlan_pm_open`/`multi_chip_loading` iterate the chip count;
  each chip has its own descriptor entry in `.data` (0x20-byte stride) and its own release
  descriptor array (`LANCHOR1+8+12*chip`) [V].
- The release word itself is the same for every chip: `CA 0x40000108 ← 0x5a5a, 4 B` [V].
- No per-core release registers exist anywhere in the module [V negative: constant census of
  `.text`/`.text.unlikely`/`.data`/`.rodata` finds no device-CA immediates other than the
  release CA, the six region types, the dev_status reads, and the conditional 0x4003A200 /
  0x40000554 / 0x400005BC / 0x40002210 writes above].
- This unit is single-chip: stock log `power on, count [1]`; the only multi-chip-gated action is
  `shuangta_pcie_enable_remap` (probe counter > 1) [V; V\* stock log].

### 2.3 ACP gate / IO windows our driver may not program?

- The SoC **ACP gate** (DT `acp-gate = <0x10100174 0x02000000 0x00>`) is applied by the
  `hi_acp` DT notifier, **not** by `hi5622v100_plat.ko`; the bootloader leaves bits 25/26 clear
  on this board [V\* pcie-seq §5]. Our transport does not touch it — that is a SoC-side difference,
  not a Wi-Fi boot prerequisite.
- The only *device-side* window programming in plat.ko is the endpoint iATU block (§1.2 rows 3-5).
  Our transport replicates the effect: 6 viewports through BAR2 `0x104 + 0x200*i`
  (BAR2 is the endpoint iATU window; the config-space twin of `0x104+0x200*i` is
  `0x908+0x200*i`) [V, our `pciet_prepare_windows` + `pciet_main.c` header contract].
- **Not MISSING but different:** the vendor programs the full runtime region table (up to 16
  entries) through config space and writes the endpoint PCI_COMMAND again afterwards; our
  transport writes 6 viewports via BAR2 and relies on `pcim_enable_device`. Equivalent for the six
  live regions [I]; a 16-viewport parity write is candidate C4.

### 2.4 What writes 0xcece (or is the gate satisfied another way)?

- **No host module writes it.** Both driver generations + the debug modules contain no 0xcece as
  raw 16/32-bit bytes, nor as an ARM `movw`/`movt` immediate, nor as a Thumb-2 `movw` immediate
  [V negative: exhaustive scan of all six files]. The only "control word" the host driver embeds is
  the release `{0x40000108, 0x5a5a, 4}` [V].
- **The firmware never writes it either**: the three `movw #0xcece` sites are compare loops
  (file 0x82102/second waiter, 0x86F74/entry-3 routine, 0xD8EC4/rare waiter) [V].
- Therefore the vendor-side peer write site is **outside** `plat.ko`/`wifi.ko` — most plausibly a
  built-in SoC/RC driver, the bootloader, or a userspace helper [U]. The *effect* is what matters:
  CA 0x4000010C must read 0x0000cece whenever one of the image's waiters executes its compare.
- Our single post-release write cannot achieve that (firmware zeroes the gate at 0x86E7E later
  than our write) [V from both sides]. **This is the defect to fix** — not the identity of the
  vendor writer.

### 2.5 Does the vendor write anything to CA 0x40000108..0x40000120 beyond 0x5a5a?

- **No.** The only write in that range is `CA 0x40000108 ← 0x00005a5a (4 B)` from the
  single-shot descriptor [V]. CA 0x4000010C is *not* written by the host driver at all [V negative
  §2.4]. The six device message registers the host maps are (from `shuangta_pcie_msg_reg_map`,
  0x1b1a0-0x1b290): 0x40039010, 0x40039014, 0x400392d4, 0x40101438, 0x40101414, 0x400392f0 [V] —
  our `hi5622_pciet.h` matches this table.

---

## 3. Diff vs our transport (missing / different actions)

| # | vendor action | our transport | verdict |
| --- | --- | --- | --- |
| D1 | **Gate service:** 0xcece must be live while the firmware spins (0x86F78), and re-served for each of the 4 entries (mask → 0x1E) | single `writel(0xcece)` immediately after release (`pciet_d2h_gate_announce`), before the firmware zeroes it | **MISSING — root cause candidate C1** [V] |
| D2 | `shuangta_pcie_pm_init` → `pcie_l1ss_dev_pinmux_set`: CA 0x40000554 ← (old\|0x220), 4 B; CA 0x400005BC ← 6, 4 B, at probe | none | **MISSING — C2** [V] |
| D3 | Endpoint iATU: full region table through config space, then PCI_COMMAND=7 again | 6 viewports through BAR2 window + `pcim_enable_device` | equivalent for live regions [I]; parity test = C4 |
| D4 | `shuangta_pcie_enable_remap`: CA 0x4003A200 ← 1 | none | vendor-gated to probe counter>1; **skipped on this single-chip unit** [V]; low |
| D5 | file-gated RMW CA 0x40002210 ← (old&0x3f)\|0x180 | none | debug/board workaround, file usually absent [U]; low |
| D6 | `pcie_msg_init`: software only (4 handler registrations) | ours registers 3 D2H handlers | vendor has more ids; not boot-critical [V] |
| D7 | Post-release: `bal_irq_enable` + HCC handler waits; no device writes | B3 P1/P2 watch attrs exist; base flow has no post-release writes either | equivalent shape; P2 drain matters later, not for the gate |

Not missing / ruled out (do not spend a session on these):
- multi-core/chip release registers (none exist),
- firmware-tail init-table host writes (vendor never does them),
- 0xcece write site in host modules (none exists),
- ACP gate (SoC-side, not Wi-Fi boot),
- extra writes in CA 0x40000108..0x40000120 (none beyond 0x5a5a).

---

## 4. Ranked candidates for the next attended session (one idea per session)

Common read map for all candidates: BAR0 offset ↔ CPU runtime R = 0x6B8000 + R; region-3 IO =
0x3B8000 + (CA - 0x40000000). Never write CA 0x400392f0. After a release, no `rmmod` — power-cycle.

| rank | candidate | exact host action | expected observable | stop condition |
| --- | --- | --- | --- | --- |
| **C1** | **Sustained 0xcece gate service after release** | after `release`, loop for 30-60 s: `writel(0x0000cece, CA 0x4000010C)` every 1-5 ms (optionally only while `out[1]==4`); keep the P2 drain disabled/off (`poll_ms=0`) for the isolated test; write the gate 32-bit | vector mask CPU 0x40008 → 0x08 then 0x1E; g (CPU 0x172130) becomes nonzero 0x0017xxxx; core-0 record CPU 0x171FB4 = {0, 0x00103FB8, ...}; state byte CPU 0x118B60 ≥ 2; out[1] bits 6/8; dmesg "Device plat ready!" then "DEVICE READY"; HCC group-4 id1/id2 seen | success; or 60 s with mask still 0x08 and out[1]=0x4 (gate writes land — CA 0x4000010C reads 0xcece — but firmware does not progress) → record, move to C2 |
| C2 | L1SS pinmux parity before release | before `release`: `writel((readl(CA 0x40000554) \| 0x220), CA 0x40000554)`; `writel(6, CA 0x400005BC)` | mask/g/state as C1 | same as C1; no change → C3 next session |
| C3 | Firmware tail init table as host writes before release | write the 9 triplets (CA ← value, 4 B): 0x40034000,0x40034014,0x40034028,0x4003403c ← 0x3a; 0x40035000,0x40035014,0x40035028,0x4003503c ← 0x3b; 0x40107000 ← 0x3a; then release + C1 loop | mask/g/state as C1 | same as C1 |
| C4 | Full 16-viewport endpoint iATU parity | program all 16 viewports (index/ctrl2/base/limit/target) with the vendor region table, disabled entries cleared, then PCI_COMMAND=7 | prepare readback 16/16; boot observables unchanged | prepare mismatch or no boot change |
| C5 | 0x4003A200 ← 1 remap enable | write CA 0x4003A200 = 1 after prepare, before upload | boot observables unchanged expected (vendor gates it out on single chip) | any device status anomaly |

**C3 caveat:** vendor evidence says the host does *not* apply the tail table (§2.1), so C3 is a
hypothesis test against the lead's hint, not a reconstruction; rank it below C1/C2.

---

## 5. Recommended first candidate — C1, full procedure

### 5.1 Preconditions (all mandatory)
1. Human at the bench; recovery door armed (`tools/recovery-card.sh` READY); bench link PASS.
2. UART capture read-only to an absolute path (`tools/uart-capture.sh`), never typed into.
3. Snapshot calibration taken; `MANIFEST.sha256` present.
4. B3 instrument built and copied; load with `insmod hi5622_pciet_b3.ko poll_ms=0`.
   `poll_ms=0` keeps the background drain off so the gate test measures the gate only.
5. No `rmmod` after release (B3 rule); end with a power-cycle.

### 5.2 Sequence (exact)
```
# 0. tool (this lane): writes log under build/uart-logs/... on the host
#    run from the host; it drives the B3 sysfs attrs remotely via tools/rsh.sh
C1_POLL_MS=1 C1_HOLD_S=45 ./tools/c1-gate-hold.sh

# what the script does, equivalently on the device shell:
cd /sys/class/<dev>/<b3-device>/        # B3 sysfs dir (see B3-PLAN)
echo 1 > prepare        # 6/6 iatu viewport readbacks OK required
echo 1 > upload         # 928920 B -> CA 0x01240000
echo 1 > verify         # diffs=0 required
# pre-release baseline reads (script logs them):
#   mask 0x6f8008, g 0x82b130, state 0x7d0b60, out[1] 0x3f1014, registry 0x831fb4
echo 1 > release        # CA 0x40000108 <- 0x5a5a
for i in $(seq 1 45000); do
    echo 1 > announce   # CA 0x4000010C <- 0x0000cece (32-bit)
    # every 1000 iterations: read mask/g/out[1] and log
    usleep 1000 2>/dev/null || sleep 0.001
done
# post-loop snapshot (script version):
echo 1 > b3_p4_state    # mask/g/state/registry reads (B3 attrs)
cat b3_rings b3_p3_hcc b3_p1_events
dmesg | tail -40
```

### 5.3 Reads and success signature
| object | BOOT0 offset | expected on success |
| --- | --- | --- |
| vector mask (CPU 0x40008) | 0x6F8008 | changes past 0x08; **0x001E** = all four entries |
| g pointer (CPU 0x172130) | 0x82B130 | nonzero 0x0017xxxx = `pcie_msg_init` ran |
| core-0 record (CPU 0x171FB4) | 0x831FB4 | +4 = 0x00103FB8; +0x10 = 0x00171F54-region |
| state byte (CPU 0x118B60) | 0x7D0B60 | ≥ 2 |
| out[1] (CA 0x40039014) | 0x3F1014 | bit 6 (0x40) / bit 3 (0x08) after drain |
| gate (CA 0x4000010C) | 0x3B810C | 0x0000CECE while serving |
| dmesg | — | "Device plat ready! (D2H message id 1)", "DEVICE READY (D2H message id 2)" |
| DR fill CA 0x4003A5CC/61C/66C/6BC +0x3C | 0x3F25CC/61C/66C/6BC | advances once D2H deposits happen |

### 5.4 Stop conditions
1. **Success** (mask 0x1E or g != 0 + ready message): stop the loop, snapshot all reads + dmesg,
   then power-cycle. Do not push further in this session.
2. **Negative A** (60 s; gate reads 0xcece during loop, mask still 0x08, out[1]=0x4):
   the gate is served but the entry routine still does not return → next session C2.
   Record UART (read-only) for the 60 s window.
3. **Negative B** (mask changes but never 0x1E; or g stays 0 after mask >= 0x1E): record exit
   point; next session re-read prelude markers (fw-ready-gate §11.3 rows 1-10) before C2.
4. **Anomaly** (kernel oops, device status garbage, host hang): power-cycle immediately,
   keep the module binary/log, do not retry in the same session.

### 5.5 Why the loop is expected to work (mechanism, not luck)
The entry routine checks the gate word continuously in a tight loop (0x86F78-0x86F7C). A 1 ms
cadence guarantees the value is present at the check within one iteration; the loop covers the
~1.8-2 s firmware-boot window and all four entries (each entry re-zeroes the gate). Draining
out[1] during the loop is *not* required for the gate, which is why `poll_ms=0` is the cleanest
first test; if C1 succeeds, a second run with `poll_ms=2` confirms the P2 drain interacts
correctly with the newly-running message layer.

---

## 6. Evidence table

| # | claim | marker | artifact / method |
| --- | --- | --- | --- |
| V1 | ready routine zeroes CA 0x40000108/0x4000010C with strh at file 0x86E80/0x86E7E | [V] | firmware blob, Thumb disasm @ CPU 0xC6E80/0xC6E7E |
| V2 | ready routine spins on word CA 0x4000010C == 0xcece at file 0x86F78 | [V] | firmware blob, movw #52942 @0x86F74 + literal 0x4000010C @0x86FA0 |
| V3 | ready routine writes out[1]=4 (0x86F5A) and CA 0x40101434=1 (0x86F66) | [V] | same disasm |
| V4 | vector mask = OR of 1<<vector into halfword CPU 0x40008 (0x86E96) | [V] | same disasm; matches fw-ready-gate §13.3 |
| V5 | firmware never writes 0xcece: 3 movw sites are compare loops | [V] | raw+imm scan of FIRMWARE.bin; disasm of 0x82102/0x86F74/0xD8EC4 |
| V6 | host modules never contain/write 0xcece | [V negative] | raw bytes + ARM/Thumb movw scan of plat/wifi 2.5.24+2.4.15+debug |
| V7 | only control-register write in boot path is CA 0x40000108 <- 0x5a5a 4 B | [V] | firmware_download 0xf8a0-0xf8b4; .data+0x26a4 descriptor; full CA constant census |
| V8 | upload is chunked raw writes to CA 0x01240000 + offset, len min(0x80000,rest) | [V] | firmware_file_send 0xf56c-0xf76c; refs @0xf724 |
| V9 | L1SS pinmux device writes: CA 0x40000554 RMW OR 0x220; CA 0x400005BC <- 6 | [V] | pcie_l1ss_dev_pinmux_set 0x1b440-0x1b4c0 |
| V10 | enable_remap writes CA 0x4003A200 <- 1, gated by probe counter > 1 | [V] | pcie_main_init 0x8b8-0x8dc + 0x1ae3c-0x1ae7c + ops table 0x29b8 |
| V11 | file-gated RMW CA 0x40002210 <- (old&0x3f)\|0x180 | [V] | pcie_main_init 0x990-0x9d4 |
| V12 | pcie_msg_init has zero device writes | [V] | call+store census of 0xb6e4-0xbb0c |
| V13 | six message registers mapped: 0x40039010/14, 0x400392D4, 0x40101438, 0x40101414, 0x400392F0 | [V] | shuangta_pcie_msg_reg_map 0x1b1a0-0x1b290 |
| V14 | firmware tail table = 9 triplets + count 0xF + DEADBEEF at file 0xE252C/0xE2598/0xE259C | [V] | hexdump (no bytes committed) |
| V15 | host does not write the tail table (no 0x40034000/0x40035000/0x40107000 constants) | [V negative] | constant census of .text/.text.unlikely/.data/.rodata both modules |
| V16 | cold boot: mask 0x08, g=0, state=1, out[1]=0x4 | [V\*] | wifi-fw-ready-gate §14 |
| V17 | vendor boot readiness timing: ready id1 ~9 ms after power-on print on stock; firmware mailbox first word ~1.85 s later on bench | [V\*]/[I] | stock UART log; phase19c fw-handshake |
| V18 | ACP gate handled by hi_acp DT notifier, not plat.ko | [V\*] | pcie-seq §5 |
| U1 | which host component (if any) actually writes 0xcece on the stock boot | [U] | not in plat/wifi; possibly built-in RC driver/bootloader/userspace |
| U2 | who applies the firmware tail init table | [U] | not host modules; no in-image reference found |
| I1 | entries 1/2/4 are dispatched after entry 3 returns; blocking at entry 3 explains mask 0x08 | [I] | consistent with mask/out[1]/g cold-boot data |
| I2 | sustained gate service will release all four entries and let pcie_msg_init/boot task run | [I] | direct consequence of V1-V3 + V16 |

---

## 7. Negative results (ruled out — do not redo)

- Host write of the firmware-tail IO table before/after release: **not in the vendor driver**.
- Additional control writes in CA 0x40000100..0x40000120 beyond 0x5a5a: **none**.
- Per-core release registers / multi-core release loop: **none** (per-chip only, count 1 here).
- 0xcece write site in host modules: **none** in either driver generation.
- ACP gate as the Wi-Fi boot prerequisite: **no** (SoC-side DT notifier, bootloader leaves it clear).
- pcie_msg_init device writes: **none** (software only).
- Debug-module register lists (0x40034000 etc.) as write tables: **no** — they are
  `oal_pcie_print_bits` dump name/address pairs.

---

## 8. Reproduction (host-only, local artifacts; nothing vendor committed)

```
TOOL=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
# vendor boot writes: full disassembly with relocations
$TOOL-objdump -dr /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko > /tmp/bootkick/plat.dis
#   -> pcie_main_init .text.unlikely 0x704; wlan_power_on .text 0xe46c;
#      firmware_download .text 0xf834; firmware_file_send 0xf56c; bal_write 0x10c04;
#      pcie_l1ss_dev_pinmux_set 0x1b440; shuangta_pcie_enable_remap 0x1ae3c;
#      shuangta_pcie_msg_reg_map 0x1b1a0; pcie_inbound_region_cfg 0x94e0
# device-CA constant census (text/data/rodata) and 0xcece scan:
python3 tools/vendor_const_scan.py   # (this lane's script; reads local modules only)
# firmware gate routine (Thumb, CPU = file + 0x40000):
$TOOL-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0xc6e14 \
  <blob-slice-0x86E14-0x86F90>   # not committed; slice built locally
```

`tools/c1-gate-hold.sh` is the next-session driver for candidate C1; it reads module paths and
the B3 sysfs prefix from environment variables and writes its log to an absolute host path.

---

## 9. C1 hardware result (2026-11-23 12:10) — NEGATIVE, and a corrected park model

Attended session (Lead): fresh host boot, B3 `poll_ms=0`, region-5 read 0xffff pre-iATU, prepare 6/6,
upload 928,920 B, verify diffs=0, P0 arm 7/7, release, then **sustained gate service**:
~2,000,000 writes of 0x0000cece to CA 0x4000010C over ~2 min (~1 per 100 us); gate register read back
0x0000cece for the whole window. Observables stayed identical to the single-write park: out[1]=0x4,
mask 0x08, g=0, state=1, out[0]=out[2]=out[5]=0. **C1 is negative.** A useful by-product: the release
word CA 0x40000108 reads 0 **after** release, i.e. the CPU consumed the kick and boot started [V\*].

### 9.1 Correction of the session's key inference (important)

The session inferred *"the CPU never reached the gate spin because out[1] bit3 (0x8) never sets, and
code at file 0x86F68-0x86F70 does out[1] |= 8"*. **That is not what the code does.**

- 0xC6F68-0xC6F70 is `ldr.w r3,[r5,#0xBE0]; orr r3,r3,#8; str.w r3,[r5,#0xBE0]` with
  r5 = literal 0x0017D398, i.e. it ORs bit3 into the routine's **private flag word at CPU 0x17DF78**
  (firmware RAM), **not** out[1] (CA 0x40039014) [V, file 0x86F68-0x86F70 + literal map §1.5].
- out[1] is written exactly twice in the image: **0x4** by this routine at file 0x86F5A [V], and
  **cleared to 0** by the firmware's `pcie_msg_init` at CPU 0x4978C (literal 0x40039014 at file
  0x97DC) [V]. Nothing writes 0x8/0xC into out[1] on this path. So "out[1] must reach 0xC if the gate
  spin is reached" is **not a valid expectation** and must not be used as a stop/success rule.

What *is* true: the routine's own progress flags are readable (see §9.2), and the observed mask 0x08
only proves the routine passed file 0x86E96 (the 1<<vector OR into CPU 0x40008); observed out[1]=0x4
proves it passed file 0x86F5A (after both trim-helper calls). **The park is at or after 0xC6F5A.**

### 9.2 Host-readable park markers (exact)

Formula: region-3 CA -> BAR0 = 0x3B8000 + (CA - 0x40000000), phys = 0x40000000 + BAR0;
CPU runtime R -> BAR0 = 0x6B8000 + R (region-5 window).

| # | marker | CA / CPU | BAR0 | phys | value tells |
| --- | --- | --- | --- | --- | --- |
| 1 | routine private flag | CPU 0x17DF78 | **0x835F78** | 0x40835F78 | 0x1 after entry (0xC6E42); \|2 at 0xC6E8C -> 0x3; \|4 at 0xC6F2C -> 0x7; \|8 at 0xC6F70 -> **0xF**. 0x7 = parked at the doorbell store or in the spin; 0xF = passed 0xC6F70 |
| 2 | D2H doorbell | CA 0x40101434 | **0x4B9434** | 0x404B9434 | 1 iff the store at 0xC6F66 executed |
| 3 | mask halfword | CPU 0x40008 | 0x6F8008 | 0x406F8008 | bit(1<<entry) set at 0xC6E96 (now 0x08) |
| 4 | halfword clear | CA 0x40000558 | 0x3B8558 | 0x403B8558 | bit0 cleared at 0xC6F30-0xC6F3C |
| 5 | trim steps / status (block A) | CA 0x4003941C / 0x40039424 / 0x40039428 | 0x3F141C / 0x3F1424 / 0x3F1428 | 0x403F141C / 0x403F1424 / 0x403F1428 | fn_407C0 (file 0x7C0) writes 22 steps: `str step -> base+0x41C`, strobe 1/0 -> base+0x428, reads base+0x424; fn_4078E (file 0x78E) reads 10 regs. Last step value persists |
| 6 | trim block B | CA 0x40039800+0x41C... | 0x3F1800... | 0x403F1800... | same for the second block |
| 7 | out[1] | CA 0x40039014 | 0x3F1014 | 0x403F1014 | 0x4 while parked; later 0 when pcie_msg_init runs |
| 8 | g pointer | CPU 0x172130 | **0x82A130** | 0x4082A130 | nonzero = pcie_msg_init ran |
| 9 | state byte | CPU 0x118B60 | 0x7D0B60 | 0x407D0B60 | boot-task progress (now 1) |

**Arithmetic correction:** the correct BAR0 for CPU 0x172130 is **0x82A130**, not 0x82B130 as in
`wifi-fw-ready-gate` §13.3/§14 (0x6B8000 + 0x172130; cross-check 0x6B8000 + 0x40008 = 0x6F8008
matches the proven mask read). The cold-boot "g=0" probe may therefore have read 0x82B130 (0x1000
high); re-read both 0x82A130 and 0x82B130 in the next session before relying on "g==0".

### 9.3 Where the CPU can be parked, ranked

- **P1 (rank 1): the doorbell store at file 0xC6F66** — `str r2,[r3]`, CA 0x40101434, in the
  0x4010_xxxx IO sub-block. A clock/pad-gated or decode-stalled IO write there wedges the CPU with no
  exception: out[1]=4 is visible, nothing after it appears. This matches every observable.
- **P2 (rank 2): the gate spin at 0xC6F74** — the CPU's load of 0x4000010C does not observe the host
  PCIe writes even though host reads do (IO ordering / aperture vs CPU-side path). C1's sustained
  service would then have been invisible to the CPU. (The routine itself stores a halfword 0 to the
  gate at 0xC6E7E first, so the CPU does touch it.)
- **P3 (rank 3): the routine returned** and the block is downstream (entries 1/2/4 are dispatched by a
  later stage; mask only grows when they are called). Then the missing action is not a gate.

**P1 vs P2 vs P3 discriminator (read-only, zero risk):** markers 1+2 above.
flag==0x7 and doorbell==0 -> P1; flag==0xF and doorbell==1 -> P2 or P3 (then compare mask/g/state over
time; if they stay constant, P3 with an off-image dispatcher is the remaining possibility and the
next step is the dispatcher path `wifi-fw-ready-gate` §11.2, not more host writes).

### 9.4 Note on the vendor path (question 3 of the follow-up)

The host-side write site for 0xcece remains absent from every vendor module [V, §2.4], so the vendor
peer is some other host/boot component [U]. But the C1 result means the gate was **not** the blocker
on our image anyway: the CPU never showed evidence of reaching the compare. The vendor boot proceeds
because the *earlier* steps of the routine complete; on our image the missing vendor-attested write is
the probe-time L1SS/pinmux pair (§9.5), which is the only device-side init vendor plat.ko performs
that our transport does not.

---

## 10. NEXT single candidate (C2): L1SS/pinmux parity at prepare time

Vendor evidence [V]: `shuangta_pcie_pm_init` (file 0x1b57c) runs from `pcie_main_init` at probe and
calls `pcie_l1ss_dev_pinmux_set` (0x1b440), which performs two device writes:

| write | CA | BAR0 | phys | value | offset |
| --- | --- | --- | --- | --- | --- |
| 1 | 0x40000554 | 0x3B8554 | 0x403B8554 | **(readl(CA) \| 0x220)**, 32-bit RMW after dsb+arm_heavy_mb | 0x1b468-0x1b490 |
| 2 | 0x400005BC | 0x3B85BC | 0x403B85BC | **6**, 32-bit | 0x1b4A4-0x1b4C0 |

Our transport does neither. This is the single new write idea for the session; the park-marker reads
above are free instrumentation in the same run.

### 10.1 Procedure
1. Attended, recovery armed, UART read-only, calibration snapshot ok.
2. `insmod hi5622_pciet_b3.ko poll_ms=0`.
3. `prepare` (6/6 readback OK needed).
4. **C2 writes:** read-modify-write CA 0x40000554 |= 0x220 (log old/new), then write 6 to CA 0x400005BC.
   Read both back; expected 0x...|0x220 and 0x6.
5. `upload` (928,920 B), `verify` (diffs=0 required), P0 arm (7/7).
6. Pre-release **park-marker baseline**: markers 1-9 of §9.2 (flag 0x835F78, doorbell 0x4B9434,
   mask 0x6F8008, 0x3B8558, 0x3F141C/24/28, out[1] 0x3F1014, g 0x82A130, state 0x7D0B60).
7. `release` (0x5a5a) then **sustained gate service** exactly as C1 (1 ms cadence, 45-60 s),
   logging markers 1+2 every ~500 iterations.
8. Post snapshot: markers 1-9 + dmesg + B3 P1/P3/P4 attrs.

### 10.2 Expected observables (success)
- Immediately after step 4: CA 0x40000554 == old|0x220; CA 0x400005BC == 6.
- During/after the run: **flag CPU 0x17DF78 advances to 0xF**, **doorbell CA 0x40101434 == 1**,
  halfword CA 0x40000558 bit0 == 0, trim-step registers stop advancing at the last step (0x16-ish
  loop counter / last value in 0x4003941C and 0x4003981C).
- Then: **mask 0x6F8008 gains bits beyond 0x08 (target 0x1E)**, **g 0x82A130 != 0**, **state
  0x7D0B60 >= 2**, out[1] leaves 0x4 (0 via pcie_msg_init, later 0x40), dmesg shows
  "Device plat ready"/"DEVICE READY", DR fill indices advance.
- **Do not use out[1]==0xC as an expectation** (§9.1).

### 10.3 Stop conditions / decision rule
1. flag reaches 0xF and doorbell becomes 1, but mask/g/state unchanged after 60 s -> the CPU passed
   the routine and the blocker is downstream (P3): stop; next session reads the dispatcher path and
   the global-object/registry state, not another IO write.
2. flag stays 0x7 and doorbell stays 0 with C2 applied -> the doorbell store still stalls (P1): the
   0x4010_xxxx sub-block is still gated. Rank-3 candidate C3 (firmware-tail IO writes, below) moves
   to rank 1 for the session after this one.
3. flag reaches 0xF and doorbell 1, mask/g move but no ready message within 60 s -> message-layer
   precondition problem: switch to the destination-registry path
   (`wifi-fw-ready-gate` §12.5/§13), not more host writes.
4. Any kernel oops/device-status anomaly -> power-cycle, keep log, no retry in-session.

### 10.4 Revised candidate ranking (after C1)
| rank | candidate | status |
| --- | --- | --- |
| 1 | **C2** L1SS/pinmux writes CA 0x40000554 \|= 0x220, CA 0x400005BC <- 6 (before release) + park-marker readout | **NEXT session** |
| 2 | C3 firmware-tail IO gate writes before release: CA 0x40034000/0x40034014/0x40034028/0x4003403C <- 0x3a; 0x40035000/0x40035014/0x40035028/0x4003503C <- 0x3b; 0x40107000 <- 0x3a (all 32-bit). Evidence against: vendor host never writes them [V]; but CA 0x40107000 is in the same 0x4010_xxxx sub-block as the doorbell store (P1) | conditional on 10.3 rule 2 |
| 3 | C4 ACP gate / host-write visibility for the device CPU (SoC side; pcie-seq addendum-2 polarity finding) | only if P2 confirmed (flag 0xF + doorbell 1 + gate serviced but no progress) |
| 4 | C5 endpoint iATU 16-viewport parity + PCI_COMMAND re-write | low |
| — | C1 sustained gate service | TESTED 2026-11-23, NEGATIVE (§9) |

### 10.5 Tooling note
The B3 module needs one small addition for this session: a WO attribute doing the two C2 writes and
a RO attribute printing the park markers (all original code, no vendor bytes). Add to
`pciet_b3.c` before rebuilding:
```c
static ssize_t b3_l1ss_prep_store(struct device *dev, struct device_attribute *a,
                                  const char *buf, size_t n)
{
        struct hi5622_pci *ctx = ...; u32 v;
        v = readl(ctx->bar0 + 0x3B8554);            /* CA 0x40000554 */
        writel(v | 0x220, ctx->bar0 + 0x3B8554);
        writel(6,        ctx->bar0 + 0x3B85BC);     /* CA 0x400005BC */
        dev_info(dev, "C2 l1ss: 0x40000554 old=0x%08x new=0x%08x; 0x400005BC=6\n",
                 v, readl(ctx->bar0 + 0x3B8554));
        return n;
}
static DEVICE_ATTR_WO(b3_l1ss_prep);
/* park markers: 0x835F78 flag, 0x4B9434 doorbell, 0x3F141C/24/28 trim,
 * 0x6F8008 mask, 0x3F1014 out1, 0x82A130 g, 0x7D0B60 state */
```
`tools/c2-park.sh` drives the session using these attributes and writes an absolute-path log.

---

## 11. Corrections carried into this revision

- §9.1: the `orr #8` at 0xC6F70 targets the private flag word CPU 0x17DF78, not out[1]; out[1] is 0x4
  by this routine and cleared by pcie_msg_init. (Supersedes any reading that expected out[1] bit3.)
- §9.2: correct BAR0 for g (CPU 0x172130) is 0x82A130; 0x82B130 in earlier docs is off by 0x1000.
- C1 expected mask 0x1E *and* g != 0 remains correct as a success signature, but it is not reachable
  through gate service alone (C1 negative); it depends on the CPU getting past file 0xC6F66.
- The firmware-tail table is confirmed again as not host-written; see §2.1 (unchanged).

---

## 12. Second hardware discriminator (2026-11-23, same parked chip) — the routine COMPLETED; g is SET

The Lead read the §9.2 markers on the still-parked chip from the C1 run (module loaded, CPU released,
no power-cycle). Results:

| marker | value | meaning under the code model |
| --- | --- | --- |
| flag CPU 0x17DF78 | **0x0000000F** | bits 1\|2\|4\|8 all set; instruction 0xC6F70 executed, i.e. the CPU passed the doorbell store AND reached the gate spin (0xC6F74) — and the sustained 0xcece service released it. **C1's mechanism worked.** |
| doorbell CA 0x40101434 | 0x00000000 | expected: the store at 0xC6F66 is the **only** writer of this CA in the image (literal 0x40101434 exists only in pcie_msg_init at CPU 0x49760-0x49764, which merely saves the address into the object); nothing clears it and no host read is required to consume it. It reads 0 as a write-only/pulse register. Not evidence of a missing store. |
| mask CPU 0x40008 | 0x08 | as expected; entries 1/2/4 still not dispatched |
| out[1] CA 0x40039014 | 0x4 | announce pending, never drained (module had poll_ms=0) |
| state CPU 0x118B60 | 1 | boot task ran its setup branch |
| trim block A CA 0x4003941C / 0x40039424 / 0x40039428 | 0x00001025 / 0 / 0 | last trim step latched (0x1025 = last table value), strobe/status idle |
| CA 0x40000558 | 0x0050 | halfword bit0 already clear, consistent with 0xC6F30-0xC6F3C having run |
| g CPU 0x172130, BAR0 0x82A130 | **0x0010C0F4** | nonzero pointer — see §12.1: pcie_msg_init RAN |

### 12.1 The g-pointer address is settled: CA 0x172130, i.e. BAR0 0x82A130

- pcie_msg_init stores the object pointer at **CPU 0x49706**: `ldr r3,[pc,#0xC8]` -> literal at CPU
  0x497CC = **0x00172130**; `str r2,[r3]` (r2 = [sp,#4], the new object pointer) [V, firmware disasm].
- The literal 0x00172130 occurs at file 0x97CC only (plus the three *reader* sites at CPU 0xC18A8 /
  0xC61DC / 0xC669C) [V]; the only writer is that store.
- Therefore the authoritative host read is **BAR0 = 0x6B8000 + 0x172130 = 0x82A130**.
  `wifi-fw-ready-gate` §13.3/§14 used **0x82B130**, which is off by 0x1000 (arithmetic slip; the mask
  check 0x6B8000+0x40008 = 0x6F8008 confirms the formula). **The stored nonzero value 0x0010C0F4 means
  pcie_msg_init ran.** The earlier cold-boot conclusion "g=0 -> message layer never initializes" is
  **withdrawn** — it was a wrong-address probe.
- The same arithmetic slip affects the registry reads in earlier docs (all off, corrected here):
  registry base CA 0x171B6C -> BAR0 **0x829B6C**; core-0 record CA 0x171FB4 -> BAR0 **0x829FB4**;
  cores 1-3 CA 0x171FCC/0x171FE4/0x171FFC -> BAR0 0x829FCC/0x829FE4/0x829FFC.
  Re-read these in the next session before re-deriving message-layer state.
- Sanity cross-check for the object: g+0x9C should hold out[1]'s CA (0x40039014) per the object layout;
  CA 0x10C0F4+0x9C = CPU 0x10C190 -> BAR0 **0x7C4190** (phys 0x407C4190) [I, from the layout doc].

### 12.2 What 0x37172 is, and how entries 1/2/4 are dispatched

0x37172 is **< 0x40000**, i.e. below the image load base: it is **ROM code**, not image code
(image occupies CPU 0x40000..0x122C98; the routine's own file 0x86E14 maps to CPU 0xC6E14) [V,
arithmetic]. So the call chain is: **ROM calls the header entry stub (CPU 0x1030E8, r1=3) -> the
routine runs -> returns to ROM at 0x37172**. The ROM is the dispatcher that calls the five header
entries (CPU 0x40050..0x40060) [I, consistent with mask bit semantics and the ROM return address].

Consequence: dispatch of entries 1/2/4 is **ROM-driven**, not gate-driven. The ROM did not call them
in the 2-minute window, so it is blocked between the entry-3 return and the next call — the most
plausible block being a firmware-side D2H publish that spins on **out[1] == 0** (the P2 flush gate
file 0x86108) before the ROM/boot flow can advance. Our C1 run left out[1]=0x4 pending for 2 minutes
(poll_ms=0, nobody serviced it); the vendor host driver services D2H continuously via
`bal_irq_enable` + the pcie_msg handlers [V\*, pcie-seq §6].

### 12.3 Revised state model (supersedes §9.3 P1/P2)

1. Boot started; release word consumed [V\*].
2. **pcie_msg_init ran** (g set to 0x0010C0F4) [V via correct probe].
3. **Entry 3 ran to completion** (mask 0x08, flag 0xF, out[1]=0x4, doorbell stored, trim blocks
   final) and returned to ROM 0x37172 [V\*].
4. The boot flow did not advance to entries 1/2/4 in 2 min with out[1] left pending [V\*].
5. Remaining missing host action: **service the D2H announce** (ack out[3] CA 0x40101438 = 1, drain
   out[1] CA 0x40039014 = 0, re-arm out[4] CA 0x40101414 = 1) **while keeping the 0xcece gate
   served** — i.e. behave like the vendor host, which does exactly this continuously.

---

## 13. NEXT candidate (C1c): sustained gate service + full D2H contract service

### 13.1 Why this ranks first (evidence)
- C1 (gate only) proved the gate mechanism works and the routine completes [V\*].
- pcie_msg_init ran; g is set [V\*].
- out[1]=0x4 was left undrained for the whole C1 window [V\*]; the firmware's own publish path
  (file 0x86108) spins while out[1] != 0 [V, wifi-fw-ready-gate §1.2/TL;DR item 5].
- The vendor host driver services D2H from the IRQ handler continuously [V\*, pcie-seq §6];
  our C1 run did not (poll_ms=0).
- C2 (L1SS pinmux) was not needed for the routine to complete (§12), so it is demoted.
- C3 (tail-table IO writes) has negative vendor evidence (§2.1) and no new support; demoted to 3.

### 13.2 Exact procedure (attended; recovery armed; UART read-only; no rmmod after release)
1. `insmod hi5622_pciet_b3.ko poll_ms=0` (keep the base poll thread OFF; we drive the contract
   explicitly so the ack/drain/re-arm order is exactly right).
2. `prepare` (6/6 readback), `upload`, `verify` (diffs=0), `b3_p0_ringinit` 7/7.
3. **Corrected baseline reads** (before release, all host reads):
   - g CA 0x172130 -> BAR0 **0x82A130** (expect nonzero 0x0010C0F4; if 0 here, pcie_msg_init has
     not run yet at this point of the boot)
   - registry core-0 record CA 0x171FB4 -> BAR0 **0x829FB4** (expect +4 = 0x00103FB8, +0x10 =
     0x00171F5x if populated)
   - registry base CA 0x171B6C -> BAR0 0x829B6C; object sanity g+0x9C -> BAR0 0x7C4190
   - out[1] 0x3F1014, out[0] 0x3F1010, mask 0x6F8008, state 0x7D0B60, DR0-3 +0x3C
4. `release` (0x5a5a).
5. **Hold loop, 60 s:** every 1 ms write 0x0000cece -> CA 0x4000010C (BAR0 0x3B810C); every 10 ms
   run the full contract service if out[1] != 0: write 1 -> CA 0x40101438 (ack, BAR0 0x4B9438),
   re-read out[1] then write 0 -> CA 0x40039014 (drain, BAR0 0x3F1014), write 1 -> CA 0x40101414
   (re-arm, BAR0 0x4B9414). The B3 `b3_drain` attribute does exactly this sequence (ack -> drain ->
   re-arm -> dispatch) [V, pciet_b3.c 0x837-0x841]; or run `b3_p1_watch` for the window.
6. Every 5 s: re-read g 0x82A130, registry 0x829FB4/0x829B6C, mask 0x6F8008, out[1] 0x3F1014,
   state 0x7D0B60, DR0-3 +0x3C.
7. **Free read-only ROM dump** (decisive for question 1): CPU 0x37000..0x37300 -> BAR0
   0x6EF000..0x6EF300 (phys 0x406EF000..0x406EF300), 0x300 bytes. This shows the ROM call site at
   0x37172 and what it does after the entry-3 return (wait loop? next entry call?). Nothing is
   written; the region-5 window already decodes CPU 0 (ROM vector read 0xE59FF018 [V\*]).

### 13.3 Expected observables (success signature)
- out[1] 0x4 -> 0 on the first drain, then either **0x40 (bit6, d2h_notify)** or a further bit
  appears (the firmware's publish path unblocks).
- **mask 0x6F8008 gains bits towards 0x1E** as the ROM dispatches entries 1/2/4.
- **registry core-0 record BAR0 0x829FB4 becomes {0, 0x00103FB8, 0x00171B7x, 0x00171E1C+, 0x00171F54+, ...}**
  (the populate block of the boot task) — the decisive message-layer check.
- **state 0x7D0B60 >= 2**; **DR0-3 +0x3C advance**; dmesg/B3 shows group-4 id1 "device plat ready"
  and id2 "DEVICE READY"; B3 P3 g4_id1/g4_id2 become 1.
- g stays 0x0010C0F4 (object pointer stable).

### 13.4 Stop conditions / next decision
1. **Success** (mask grows / registry populated / ready message): snapshot everything, power-cycle;
   hand the Lead the ready-gate/post-ready lane.
2. out[1] reposts 0x4 on every drain (i.e. the firmware re-announces in a loop) but mask/g/state do
   not move after 60 s of correct ack/drain/re-arm: the D2H service is not the blocker -> next session
   is ROM-dump analysis (post-0x37172 flow) and the boot-task registry path, **not** C2/C3.
3. mask reaches 0x1E but no ready message: message-layer destination/send problem — switch to
   `wifi-fw-ready-gate` §12.5/§13 with the corrected registry offsets.
4. Anomaly/oops: power-cycle, keep log, no retry in-session.

### 13.5 Ranking after this revision
| rank | candidate | verdict |
| --- | --- | --- |
| 1 | **C1c** sustained 0xcece + full D2H ack/drain/re-arm contract service (+ ROM dump reads) | **NEXT session** |
| 2 | C2 L1SS/pinmux writes (CA 0x40000554 \|= 0x220, CA 0x400005BC <- 6) | demoted: routine completed without it |
| 3 | C3 firmware-tail IO gate writes (incl. CA 0x40107000 <- 0x3a) | demoted: negative vendor evidence, no new support |
| — | C1 gate-only service | TESTED NEGATIVE for progress, but proved the gate mechanism (§12) |

### 13.6 Answer to the doorbell question
No firmware code clears CA 0x40101434. The only writer is the routine's store at file 0xC6F66; the
only other reference is pcie_msg_init saving the address into its object (CPU 0x49760-0x49764), and it
does not store to it. Host reads of 0 are therefore expected (write-only/pulse register) and do not
contradict flag=0xF: the store executed.

---

## 14. C1c hardware result (2026-11-23, warm chip) — NEGATIVE; what actually ran

Facts (Lead): 80 s of 1 ms gate writes + full ack/drain/re-arm; out[1] 0x4 -> 0 and stayed 0;
**nothing else moved**: mask 0x08, g 0x82A130 = 0x0010C0F4, core-0 record 0x829FB4 =
{0, 0x00103FB8, 0x00171B70, 0x00171E1C, 0x00171F54, 0x00171FA8, 0, 0} (populated), state 1,
DR fill 0, flag 0x0F, object sanity g+0x9C = 0x40039014. D2H service is not the blocker.
**Dismissed hypotheses:** "gate not served", "spin not reached" (C1 completed it), "registry empty"
(withdrawn: earlier probes read the wrong BAR0 offsets), "out[1] drain blocks boot" (C1c).

### 14.1 What populated the registry (corrected; was wrong in fw-ready-gate §12.5)

The registry populate block is a **prelude step of the boot task's first pass**, not a later pass:
- boot task entry CPU 0x47628 (file 0x7628); at 0x477EC-0x4782A it initialises an object and calls
  `[obj+0x10]+8` then `[obj+0x10]+4`; **only if that callback returns non-zero** it jumps
  `beq.w #0x47ADC` (0x4782A) into the populate block [V].
- populate block 0x47ADC-0x47B2A: `memset(CA 0x171B6C, 0, 0x490)` at 0x47ADC-0x47AE6 [V], then
  walks the 0x2C-stride config table and fills the 24-byte records [V, matches the live record].
- then the flow reaches the state check 0x47D0A-0x47D18 with the registry already populated [V].
So on our chip the callback succeeded and populate ran **before** the state check in the same pass.

### 14.2 What the boot task's one pass did, and where it stopped

- state check: `ldrb r6,[r4+0xF60]` (state; r4 = CA 0x117C00 global object) -> 0; `cmp #1; bls 0x48160`
  (setup branch).
- setup branch 0x48160: **state = state+1 (0 -> 1)** at 0x48174 [V]; builds the HCC callback/thread
  objects at [r4+0x214..0x250], list heads at [r4+0x270..0x27C]; then `b #0x47D28` (0x4828A) [V].
- 0x47D28: builds the **id-1 message** (header byte0=4, byte6=1) and calls the send primitive
  CPU 0x102760 (file 0xC2760) at 0x47D54 [V]. If it returns 0 it takes the 0x49188 path (which builds
  another frame and calls msg-alloc 0x102822 with id 0x3d); return value is not stored anywhere.
- the task disarms the reset-dispatch slot **CA 0x17D42C <- 0x000C19D1 (BUG stub)** at 0x47D06-0x47D08
  [V] (the slot the reset dispatcher CPU 0xC2EEC loads from CA 0x17D398+0x94 and blx's; the dispatcher
  is reachable only from the reset stub CPU 0x103140 — no in-image caller [V negative BL scan]).
Observed signature (no out[1] bit6, DR fill 0, state 1) is consistent with the id-1 send failing or
blocking **inside** the send primitive, or with the task never getting past the enqueue because the
HCC thread objects built just before are not running yet. It is NOT a destination-registry problem.

### 14.3 How entries 1/2/4 are dispatched (and why mask 0x08 is not the blocker)

The five header words at CA 0x40050 (file 0x50) are **exception-vector stubs** (reset 0x103140,
undef 0x103114, svc 0x1030E8, prefetch 0x1030BC, abort 0x103090); the routine ORs `1<<index` into the
halfword at CPU 0x40008 per entry. Mask 0x08 therefore means **only the SVC entry has run** — it is an
exception-seen bitmap, not an init-stage dispatch table [V, code at 0xC6E96; confirmed by flag 0x0F
and the routine's return]. The other entries are taken only if those exceptions occur; their absence
is expected and does not by itself block the boot flow.

---

## 15. ROM readability: the right window is the region-0 identity window (read-only test)

- The failed read CPU 0x37172 -> BAR0 0x6EF172 (region-5 / **ACP** window: CA = CPU + 0x1200000)
  returns 0xFFFFFFFF. That is consistent with the ROM not being exposed through ACP.
- The endpoint's region table has a second, **identity** window: region 0 "ROM_WRAM",
  host BAR0 0x000000-0x1BFFFF -> device CA 0x000000-0x001BFFFF (our transport programs this viewport;
  `hi5622_regions[0] = {bar 0x0, limit 0x1BFFFF, target 0x0}`). This is the direct ROM/WRAM view.
- Therefore the ROM read path to test is **BAR0 offset = CPU address** (phys 0x40000000 + CPU),
  i.e. CPU 0x37172 -> BAR0 **0x37172** (phys 0x40037172), *not* 0x6EF172.
- Free read sweep to settle the map (all reads; compare against three known values:
  mask CPU 0x40008 = 0x08, state CPU 0x118B60 = 1, g CPU 0x172130 = 0x0010C0F4):
  region-0 identity: 0x0, 0x4, 0x50, 0x100, 0x1000, 0x10000, 0x20000, 0x30000, 0x37000,
  **0x37160..0x371C0**, 0x3FFF0, 0x40000, 0x40008, 0x40050, 0x118B60, 0x172130, 0x17D42C;
  region-5 ACP: the same CPU addresses at BAR0 0x6B8000+R for the three known words only
  (0x6F8008/0x7D0B60/0x82A130) — whichever window reproduces all three is the CPU-RAM alias.
- If the identity window reproduces the known values, disassemble ROM at 0x37172 and follow the call
  chain that returns from the SVC handler (this answers "what does the ROM do next" directly).
  If **both** windows fail for 0x37172 while the three known words read correctly, the ROM body is
  read-protected and the dispatcher cannot be recovered from the host: infer from image evidence only.

---

## 16. NEXT candidate: C1d — re-run the boot chain (CPU re-kick) with gate + D2H service held

There is no host-RAM poke that can re-enter the boot task: the only caller is the reset dispatcher
0xC2EEC, reachable only from the reset stub, and the task disarms its own slot with a BUG stub [V].
The only host-visible control that starts/restarts the CPU image boot is the **release trigger
CA 0x40000108** (the firmware zeroes it at file 0x86E80 after boot; it reads 0 now) [V]. Candidate:
re-issue the release word on the warm, fully-booted chip so the ROM/image boot chain runs again; with
internal RAM preserved the boot task's second pass should run (state 1 -> 2) and the id-1 send should
be attempted against an already-populated registry.

### 16.1 Free reads to take first (same session, no writes)
1. Region-0/region-5 mapping sweep of §15 (settles where the ROM lives).
2. Boot-task global object CA 0x117C00 (BAR0 0x7CFC00 via region-5): +0x214/+0x218/+0x21C/+0x220/
   +0x224/+0x228/+0x230/+0x234/+0x238/+0x23C/+0x240/+0x244/+0x248/+0x24C/+0x250 (HCC thread/callback
   objects built in the setup branch), +0x270/+0x274/+0x278/+0x27C (queue list heads), +0xEEC
   (=0x118AEC late-prelude marker), +0xF60 state.
3. Reset-dispatch slot CA 0x17D42C (BAR0 0x83542C): expect **0x000C19D1** (disarmed BUG stub). If it
   still reads 0x00047629 the prelude's disarm was skipped and a dispatch retry has a target.
4. Dispatcher base+0xBE0 flag CA 0x17DF78 = 0xF (known), lock object CA 0x17DF44 (BAR0 0x835F44) w0
   (expect 0 = unlocked), staged SVC frame CA 0x4000C..0x4004C (BAR0 0x6F800C..0x6F804C; lr/lr_exc/
   spsr words at +0x3C/+0x40/+0x44) to confirm the SVC context and return target.
5. MessageBox object g CA 0x10C0F4 (BAR0 **0x7C40F4**): +0x00..+0x20, +0x98..+0xC0 (out pointers and
   the +0xB4/+0xB8 armed/pending shadow), +0xD0..+0xE8 (the six mapped message CAs: out[1]=0x40039014,
   doorbell 0x40101434, out[0]=0x40039010), +0xF8, +0x128.
6. Message registers: out[0] 0x3F1010, out[1] 0x3F1014, out[2] 0x3F22D4 (CA 0x400392D4), out[3]
   0x4B9438 (CA 0x40101438), out[4] 0x4B9414 (CA 0x40101414); ETE intr 0x3F1508; SR/DR enables
   CA 0x4003A400/450/4A0 (SR), 0x4003A590/5E0/630/680 (DR) +0x00.
7. DR0-3 fill CA 0x4003A5CC/61C/66C/6BC (BAR0 0x3F25CC/61C/66C/6BC).

### 16.2 Write step (single idea)
1. Keep the module loaded (poll_ms=0), chip parked, UART read-only, recovery armed.
2. **Start the service loop first** (as C1c): 0xcece -> CA 0x4000010C every 1 ms; ack/drain/re-arm the
   D2H contract whenever out[1] != 0 (b3_drain every 10 ms).
3. **Re-kick the boot chain:** write 0x00005a5a (32-bit) to CA 0x40000108 (BAR0 0x3B8108).
4. If the dispatcher slot CA 0x17D42C still reads 0x00047629 after step 3's boot activity settles and
   state has not advanced, optionally re-arm it (write 0x00047629) — only as a follow-up, not first.
5. Watch for 60 s: state byte 0x7D0B60 (expect 2 if the boot task re-enters), out[1] (expect bit6
   0x40 / bit3 0x08 when the id-1/id-2 sends publish), DR fill +0x3C advance, g 0x82A130 change,
   mask 0x6F8008 change (a re-run may set other exception bits or reset the mask), dmesg, and the
   staged frame. Then re-read the full free-read set of §16.1.

### 16.3 Expected outcomes / stop conditions
- state >= 2 and/or out[1] gains bits and DR fill advances -> boot chain re-entered, id-1 path live:
  snapshot, power-cycle, hand to ready-gate lane.
- mask/state/g unchanged after 60 s -> the re-kick is ignored on a running CPU. Then the next lane is
  (a) ROM-window result analysis and (b) a **true cold power-cycle with the corrected read set**
  (previous cold-boot verdict used the wrong g/registry offsets and must be re-run), not another poke.
- mask/frame change but no id-1 -> the boot chain re-ran but the send primitive still fails; then the
  next lane is the send primitive itself (file 0xC2760) with the live g-object fields from §16.1(5).
- device anomaly -> power-cycle; do not retry in-session.

### 16.4 Ranked candidates after C1c
| rank | candidate | evidence |
| --- | --- | --- |
| 1 | **C1d** re-kick CA 0x40000108 <- 0x5a5a with gate+D2H service held (boot-chain re-run) | only host lever that starts the CPU image boot; boot task has no other caller; task disarms its slot |
| 2 | C2 L1SS/pinmux writes (CA 0x40000554 |= 0x220, CA 0x400005BC = 6) | vendor-attested, still absent; demoted (boot task already ran through populate+state+send without it) |
| 3 | C3 firmware-tail IO writes (incl. CA 0x40107000 <- 0x3a) | negative vendor evidence; demoted further |
| — | C1/C1c gate and D2H service | proven mechanisms, not the blocker |

