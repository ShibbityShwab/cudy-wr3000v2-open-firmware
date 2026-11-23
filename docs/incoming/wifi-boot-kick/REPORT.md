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
  own IO init touches at boot [I].

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