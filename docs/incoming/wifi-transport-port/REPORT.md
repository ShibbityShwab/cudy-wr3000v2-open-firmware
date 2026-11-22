# Hi5622 PCIe/ETE transport port — 6.18 skeleton (clean-room)

**Lane:** wifi-transport-port (host-only until 2026-11-22, then attended bench on the
project unit). **Written:** 2026-11-22, **updated after the stage A/B1/B2 bench runs.**
**Deliverable:** original C (no vendor object code, no vendor source text, no blobs) under
build/ + this report + BENCH-PLAN.md + census/family-sizes.csv. The module **compiles**
against the project's 6.18.60 tree into hi5622_pciet.ko (license=GPL, vermagic
6.18.60 SMP mod_unload ARMv7 p2v8) and was **loaded and executed on the board**: the
transport contract below is bench-verified end-to-end - see section 0.

**Builds on:** wifi-framework-port (M3 skeleton + census), wifi-driver-re/REPORT.md
(mailbox/doorbell write order, D2H dispatch, hcc header, ETE registers),
wifi-firmware-re/REPORT.md (upload/release/gate/ready contract, dev_status_check),
phase18/inbound-map.md (endpoint iATU viewports), phase42/ete-registers-reconciled.md (ETE
channel registers, device window 0x8xxxxxxx), phase43/device-dr-side.md (D2H preconditions).
All addresses below are the decoded **contract** values; evidence markers follow the project
convention ([V] re-verified from a named local artifact or bench log, [V*] device-measured by
an earlier attended lane, [I] inference, [U] unknown).  No per-unit value, vendor binary,
firmware blob or calibration data is committed.

## 0. Bench status (2026-11-22): the transport contract is device-verified [V]

Host link context: the two Hi5622 endpoints (59e7:0005, class 0x028000) only enumerate on
6.18 after the board GPIO0 20/21/28 fix in the pcie-luofu host driver (patch 013); with it
both endpoints probe and every transport access below uses the RC outbound MEM window.

| stage | result | evidence |
| --- | --- | --- |
| **A - enumerate** | both endpoints probed: `0000:00:00.0` BAR0 0x40000000 / BAR2 0x41800000, `0001:00:00.0` BAR0 0x58000000 / BAR2 0x59800000; device memory reads work; ETE rings allocated | parent ledger 2026-11-22 17:45 [V] |
| **B1.1 prepare** | all six endpoint iATU viewports programmed and read back OK, `windows=1` | ledger 18:20 [V] |
| **B1.2 upload** | 928,920 B firmware copied to BAR0+0x6f8000 in 2 chunks, no stall | ledger 18:20 [V] |
| **B1.3 verify** | **read-back of the target window BAR0+0x6f8000 over the whole blob: `file=928920 bytes diffs=0 match=YES`** | ledger 18:20 [V] |
| **B1.4 release** | `0x5a5a` written to CA 0x40000108, `cpu_released=1` | ledger 18:20 [V] |
| **B1.5 announce** | `0xcece` written to CA 0x4000010c, `gate_announced=1` | ledger 18:20 [V] |
| **B1.6 ready** | D2H bitmap `0x00000004` -> **DEVICE READY (D2H message id 2)**, `ready2=1 d2h=1`; status words match the stock vendor run (efuse_chip_id 0x186, dcoldo_vset 0x260d4184, dcoldo_efuse 0x8108, pbank_code 0x312, chip CPU executing) | ledger 18:20 [V] |
| **B2 - ETE + H2D** | RC inbound window programmed by the DWC framework from the board DTS `dma-ranges` (vp0 ctrl2=0x80000000 base=0x80000000 limit=0x8fffffff target=0x80000000); ETE SR0 ch 0x400 base 0x8360c000 and DR0 ch 0x590 base 0x83634000 programmed, depth-1 0x1f; H2D smoke: 64 B on SR0, doorbell mask 8 (id 3), no abort, device status live | parent ledger 2026-11-22 18:35, image W26a [V] |

Timing lesson for the driver: the device posts **DEVICE READY about 3 s after the CPU
release**.  The poll thread now latches its first scan 2 s late and polls every 50 ms
(commit 9700250); with a one-shot poll fired too early the bitmap still reads 0x0.

What this does **not** prove: ETE data movement beyond the H2D smoke (no payload round
trip), HCC message semantics, the hdpp/alg/hmac data path, or any netdev/mac80211 layer.
The transport is the bus/lifecycle layer, not a Wi-Fi driver.

## 1. Register/window map onto the luofu PCIe host driver

The transport sits behind the DWC-based luofu root complex (013-arm-pci-luofu-host.patch,
drivers/pci/controller/dwc/pcie-luofu.c).  The host driver enumerates the endpoint
(59e7:0005, class 0x028000) at bus 0 dev 0 fn 0 of each RC, programs the MEM/IO **outbound**
windows from ranges, serves configuration with aligned-dword accessors, and programs the
**inbound** window from `dma-ranges` when the board DTS supplies one - the board DTS now
does (commit 5a6a500; verified on bench image W26a), so device->host DMA works.  The driver
still has no MSI/INTx (its msi_init returns 0, so has_msi_ctrl=false and no MSI domain is
created; the endpoint's INTx status/clear registers sit in the never-read misc window,
patch TODO).  Every transport address below is an **endpoint BAR0 offset**, reached through
the RC outbound MEM window.

| contract item | device CA | BAR0 offset (rule: 0x3b8000 + (CA - 0x40000000)) | source |
| --- | --- | --- | --- |
| firmware target (region 5) | 0x01240000 | +0x6f8000 (alias read-back +0x40000) | firmware-re sec 2.1 [V*] |
| CPU release word | 0x40000108 | +0x3b8108 | firmware-re sec 2.2 [V*] |
| D2H announce gate | 0x4000010c | +0x3b810c | firmware-re sec 2.4 / phase43 P4 [V] |
| H2D pending mask (out[0]) | 0x40039010 | +0x3f1010 | driver-re sec 4.2 [V*] |
| D2H message bitmap (out[1]) | 0x40039014 | +0x3f1014 | driver-re sec 4.2 [V*] |
| H2D doorbell (out[2]) | 0x400392d4 | +0x3f12d4 | driver-re sec 4.2 [V*] |
| **out[5] - NEVER WRITE** | 0x400392f0 | — | project hard rule [V*] |
| twin/ack word (semantics [U]) | 0x40039800 | +0x3f1800 | driver-re sec 4.2 [V*]/[U] |
| MAC-side msg regs (out[3]/out[4]) | 0x40101438 / 0x40101414 | +0x4b9438 / +0x4b9414 | driver-re sec 4.2 [V] |
| D2H doorbell (host read-only) | 0x40101434 | +0x4b9434 | driver-re sec 4.2 [V] |
| ETE channel file (7 x 0x50) | 0x4003a000 | +0x3f2000 | phase42 E.2 [V*] |
| ETE intr/mask word | 0x40039508 | +0x3f1508 | phase42 E.2 [V*] |
| 11 x dev_status_check | sec 2.5 table | +0x3b82a8 .. +0x4b9234 | firmware-re sec 2.5 [V*] |

**Endpoint iATU (BAR2) - the compact 7-word layout, absolutely addressed [V, corrected on
the bench]:** six inbound viewports at BAR2 + 0x104 + 0x200*i, each **7** words:
{+0x00 ctrl2 = (bar&7)<<8 | 0x80000000, +0x04 base_lo, +0x08 base_hi, +0x0c limit_lo,
+0x10 target_lo (device CA), +0x14 target_hi} - **there is no limit_hi word** (unlike the
DWC 8-word viewport), and **base/limit are absolute PCI bus addresses** (BAR0 resource
start + region offset), not BAR-local offsets.  Programming them BAR-local was the early
B1 failure mode (only region 0 decoded); with the compact/absolute form all six viewports
read back and the firmware window decodes.  The six programmed regions (host BAR0
base/limit -> CA target): idx 0 0x000000-0x1bffff -> 0x00000000 (ROM_WRAM), 1
0x1c0000-0x1d7fff -> 0x00400000 (TCM), 2 0x1d8000-0x3b7fff -> 0x01000000 (PKTRAM), 3
0x3b8000-0x4d7fff -> 0x40000000 (IO), 4 0x4d8000-0x6b7fff -> 0x02000000 (ACP), 5
0x6b8000-0x8cffff -> 0x01200000 (ACP/firmware) (phase18 A.4-A.6 [V]/[V*], bench-confirmed).

**ETE channels:** SR0-SR2 at block offsets +0x400/+0x450/+0x4a0 (host ring in program group A
+0x10..+0x1c), DR0-DR3 at +0x590/+0x5e0/+0x630/+0x680 (host ring in group B +0x30..+0x3c);
per-channel enable +0x00 bit 0, ctrl +0x08, resource +0x28, second enable +0x48
(driver-re sec 4.1, phase42 E.2 [V*]).  Ring node = 8 B {word0 = device-side address of the
host-DRAM buffer, word1 = (len<<16) | 0x6d2b} [V].  Measured live shape: base in device window
0x80000000..0xffffffff, depth-1 = 0x1f, indices <= 0x7ff with a 0x400 wrap-phase bit
(phase42 E.3 [V*]).  Bench: SR0/DR0 were programmed and the H2D smoke passed (section 0).

**The host-driver delta:** the board DTS now carries
`dma-ranges = <0x02000000 0x0 0x80000000 0x80000000 0x0 0x10000000>` on both PCIe nodes
(commit 5a6a500; 256 MiB identity window).  Device-side 0x8xxxxxxx values (phase42/43 [V*])
resolve to host DRAM, which starts at phys 0x80000000, so the mapping is identity, and the
DWC framework programs the RC inbound viewport (bench-verified: vp0 base 0x80000000 limit
0x8fffffff).  `build/luofu-host-618-delta.patch` is the original mapping note for this
change (it proposed a 2 GiB window and is superseded by the committed 256 MiB DTS window;
keep it as history, do not apply).

**Portability trap documented in code:** the vendor's 5.10 tree ran a 2 GiB userspace/kernel split
where host VA == device address; our 6.18 config is **VMSPLIT_3G**, so that identity must NOT be
copied.  The skeleton always derives device-side addresses from dma_alloc_coherent() dma
handles, which are numerically the 0x8xxxxxxx values the ETE engine expects once dma-ranges
lands.

**IRQ wiring (documented gap, not fixed here):** on 6.18 there is no MSI (host msi_init
returns 0) and no INTx demux (radm status/clear are in the never-read misc window); the stock
5.10 numbering was IRQ 207/209 ([V*]).  The skeleton therefore serves D2H with a **polled bitmap
scan** (read-only on BAR0 + the contract drain write); a request_irq() path is compiled in
behind use_irq=1 for when the host side resolves INTx.  Bench: the polled scan is what
observed the D2H READY message.

## 2. What the skeleton implements (contract -> code)

build/hi5622_pciet.h (contract constants + structs), build/pciet_main.c (module) and
build/pciet_ete.c (rings), all original text.  Mapping:

| vendor function (decoded) | skeleton | bench status |
| --- | --- | --- |
| oal_pcie_set_inbound (BAR2 branch) [V] | pciet_prepare_windows() | **PASS**: 6/6 viewports read back, compact 7-word/absolute form (section 1) |
| firmware_file_send / pcie_write [V] | pciet_fw_upload() | **PASS**: 928,920 B in 0x80000 chunks |
| verify (phase18 readback discipline) [V] | verify sysfs attr | **PASS**: target window BAR0+0x6f8000, whole blob, diffs=0 |
| single-shot bal_write(0x40000108, 0x5a5a) [V] | pciet_cpu_release() | **PASS**: cpu_released=1 |
| 0xcece D2H gate [V]/[U] | pciet_d2h_gate_announce() | **PASS**: gate_announced=1 |
| pcie_ete_init_src/dst_ring [V*] | pciet_ete_ring_alloc/init() | **PASS** (programming): SR0 0x8360c000 / DR0 0x83634000, depth-1 0x1f; enable bits deliberately untouched (vendor order [U]) |
| shuangta_ete_sr_dscr_fill [V] | pciet_ete_sr_send() | **PASS** (smoke): 64 B on SR0, no abort |
| pcie_msg_send_irq [V] | pciet_msg_send_irq() | **PASS** (smoke): mask -> drain-poll -> doorbell, mask 8 / id 3 |
| pcie_msg_handle [V] | pciet_d2h_scan() | **PASS**: bitmap 0x4 -> DEVICE READY id 2 (ready2=1 d2h=1); ids 1/3 not observed (observation, not a failure) |
| boot table ids 1/2 (.data+0x2660) [V] | pciet_d2h_plat_ready/host_ready | id 2 handler ran; id 1 (plat-ready) was not posted in the poll window |
| dev_status_check [V*] | pciet_dev_status_show() | **PASS**: live status words match the stock vendor run |

**Safety model:** probe() writes nothing to the device (only PCI-core enumeration + ioremap +
host-DRAM allocation); every device write is a single explicit sysfs store (prepare, upload,
release, announce, ringinit, send, poll) with dmesg logging; BUILD_BUG_ON asserts no writable
offset collides with the never-write out[5] and a runtime gate sits on the only write helper.
Read-only checkpoints: out1, ready1, ready2, d2h_count, windows, status.  The bench used this
single-step model throughout, with recovery armed.

## 3. Build status (what compiles)

hi5622_pciet.ko - GPL, vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, 44 function symbols,
**zero warnings with -Werror**, built with arm-openwrt-linux-muslgnueabi-gcc (14.4.0)
against the built 6.18.60 tree.  Build line and fix log: build/BUILD-NOTES.md.  The
bench-built module has **sha256 416d1db0...** (short prefix; the .ko is not committed, only
text is).  **BUILD-NOTES.md previously recorded an older build hash (87fb532d...); that hash
and its 93,468 B size are stale** - see the updated notes there.  The source in this
directory is the source of the bench-built module.

## 4. What remains (transport slice)

1. **ETE data movement:** rings are programmed and the H2D doorbell smoke passed, but no
   DR round trip / payload exchange has been exercised.  The enable sequence (+0x00/+0x08/
   +0x48) is still [U] and deliberately untouched.
2. **Twin word (+0x3f1800) [U]:** off by default; not exercised.
3. **Message semantics:** the hcc header (driver-re sec 3.1) is decoded but unused beyond
   logging; the two wire-header readings (u16@+4 flag/id vs length) need a real message
   exchange to arbitrate (driver-port lane / hcc-re contract).
4. **INTx delivery:** blocked by the never-read misc window; polled D2H is the standing
   design.
5. **DR payload correctness / VMSPLIT:** node devva-to-VA resolution is table-based; the
   0x400 index phase-bit is a [V*] default, and the vendor's host-VA==device-address
   identity must never be copied on 6.18 (VMSPLIT_3G).
6. **Wi-Fi driver:** none of this is a mac80211/cfg80211 driver; the hdpp/alg/hmac/wal
   layers are separate slices (section 5).

## 5. Next slices (from census/family-sizes.csv, extracted from the vendor .ko symbol sizes)

| slice | family | funcs | bytes | stubs <=16B | avg B | note |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| **this lane** | pcie_ + bal_ | 127 | 26,124 | 30 | — | transport core (bal_ collapses to the PCIe path: 9/26 stubs) |
| **this lane** | + hcc_ (msg layer) | 213 | 38,184 | 44 | — | 86/87 hcc_ fns are ko-only |
| transport group | + shuangta_ + hal_ | 391 | 67,432 | 60 | — | the ~391 fns of the task statement |
| next | **hdpp_** | 238 | 52,428 | 14 | 220 | data path; hi1105 dpe_ is the semantic reference, names differ |
| next | **alg_** | 222 | 97,700 | 0 | 432 | densest family; 414-entry alg/cali table (firmware-re sec 5.9) |
| next | **hal_** remainder | 91 | 4,832 | 10 | 53 | mostly tiny accessors; Hi1152 datasheet cross-check |
| then | **shuangta_** | 173 | 36,476 | 39 | 210 | chip-codename layer, no public source |
| long tail | hmac_ ko-only | 1,503 | 511,364 | 118 | 340 | MAC/MLME feature delta |
| long tail | wal_ ko-only | 684 | 163,476 | 246 | 239 | private ioctl / vendor userland ABI |
| long tail | oal_/oam_/mac_ ko-only | 419 | 78,420 | 53 | — | extend the M3 skeleton |

Sizing totals: the transport group is about 67 KB of function code (about 6.6 % of the
1,014,560 B ko-only delta); hdpp + alg are about 150 KB; the hmac/wal long tail dominates
(about 675 KB).  Sequencing per wifi-framework-port M4: transport (this lane, done to the
B1/B2 contract level) -> hdpp -> alg/hal/shuangta -> hmac/wal ko-only.  The closed firmware
keeps any full result upstream-impossible; vendor GPL source remains the fast path, and
these numbers are the cross-check list for whatever source arrives.

## 6. Evidence ledger

| claim | artifact / command | grade |
| --- | --- | --- |
| module builds clean on 6.18.60 | build line in build/BUILD-NOTES.md; module sha256 416d1db0... | [V] |
| transport contract device-verified (prepare/upload/verify/release/announce/ready) | parent `mem-entries.md` 2026-11-22 18:20; bench logs in build/ (local, gitignored) | [V] |
| dma-ranges inbound window + ETE rings + H2D smoke | parent `mem-entries.md` 2026-11-22 18:35; bench image W26a sha d03a62e5 | [V] |
| register/window constants | wifi-driver-re sec 4, wifi-firmware-re sec 2/3, phase18 A.4-A.6, phase42 E.2/E.3 | [V]/[V*] |
| endpoint iATU compact 7-word layout, absolute base/limit | phase18/inbound-map.md A.4-A.6 + bench correction (six viewports read back, firmware window decodes) | [V] |
| doorbell write order / D2H dispatch | objdump decode quoted in wifi-driver-re sec 4.2; bench D2H bitmap 0x4 | [V] |
| family byte sizes | census/family-sizes.csv (pyelftools STT_FUNC st_size sums over the two vendor .ko) | [V] |

**Redaction / scope:** no vendor .ko/firmware/calibration/INI committed; the CSVs carry
counts and sizes only (function names already live in the framework census).  The bench
runs were attended with the recovery door armed; the module performs no device writes at
probe time by construction - every write is an explicit sysfs step.
