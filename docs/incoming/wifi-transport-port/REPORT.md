# Hi5622 PCIe/ETE transport port — 6.18 skeleton (clean-room)

**Lane:** wifi-transport-port (host-only; no device contact). **Written:** 2026-11-22.
**Deliverable:** original C (no vendor object code, no vendor source text, no blobs) under
build/ + this report + BENCH-PLAN.md + census/family-sizes.csv. The skeleton **compiles**
against the project's 6.18.60 tree into hi5622_pciet.ko (93,468 B, ELF32 ARM, license=GPL,
vermagic 6.18.60 SMP mod_unload ARMv7 p2v8) - see build/BUILD-NOTES.md.

**Builds on:** wifi-framework-port (M3 skeleton + census), wifi-driver-re/REPORT.md
(mailbox/doorbell write order, D2H dispatch, hcc header, ETE registers),
wifi-firmware-re/REPORT.md (upload/release/gate/ready contract, dev_status_check),
phase18/inbound-map.md (endpoint iATU viewports), phase42/ete-registers-reconciled.md (ETE
channel registers, device window 0x8xxxxxxx), phase43/device-dr-side.md (D2H preconditions).
All addresses below are the decoded **contract** values; evidence markers follow the project
convention ([V] re-verified from a named local artifact, [V*] device-measured by an earlier
attended lane, [I] inference, [U] unknown).  No per-unit value, vendor binary, firmware blob or
calibration data is committed.

## 1. Register/window map onto the luofu PCIe host driver

The transport sits behind the DWC-based luofu root complex (013-arm-pci-luofu-host.patch,
drivers/pci/controller/dwc/pcie-luofu.c).  The host driver today: enumerates the endpoint
(59e7:0005, class 0x028000) at bus 0 dev 0 fn 0 of each RC, programs the MEM/IO **outbound**
windows from ranges, serves configuration with aligned-dword accessors, and deliberately does
**not** program inbound windows (dma-ranges absent) nor MSI/INTx (its msi_init returns 0, so
has_msi_ctrl=false and no MSI domain is created; the endpoint's INTx status/clear registers sit
in the never-read misc window, patch TODO).  Every transport address below is an **endpoint BAR0
offset**, reached through the RC outbound MEM window - no host-driver change is needed for any
stage-B1 access.

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

**Endpoint iATU (BAR2):** six inbound viewports at BAR2 + 0x104 + 0x200*i; per viewport
{+0x00 ctrl2 = (bar&7)<<8 | 0x80000000, +0x04 base_lo (BAR0-relative), +0x08 base_hi,
+0x0c limit_lo (= base+size-1), +0x10 limit_hi, +0x14 target_lo (device CA), +0x18 target_hi}
(phase18 A.4-A.6 [V]/[V*]).  The six programmed regions (BAR0 base/limit -> CA target): idx 0
0x000000-0x1bffff -> 0x00000000 (ROM_WRAM), 1 0x1c0000-0x1d7fff -> 0x00400000 (TCM), 2
0x1d8000-0x3b7fff -> 0x01000000 (PKTRAM), 3 0x3b8000-0x4d7fff -> 0x40000000 (IO), 4
0x4d8000-0x6b7fff -> 0x02000000 (ACP), 5 0x6b8000-0x8cffff -> 0x01200000 (ACP/firmware).

**ETE channels:** SR0-SR2 at block offsets +0x400/+0x450/+0x4a0 (host ring in program group A
+0x10..+0x1c), DR0-DR3 at +0x590/+0x5e0/+0x630/+0x680 (host ring in group B +0x30..+0x3c);
per-channel enable +0x00 bit 0, ctrl +0x08, resource +0x28, second enable +0x48
(driver-re sec 4.1, phase42 E.2 [V*]).  Ring node = 8 B {word0 = device-side address of the
host-DRAM buffer, word1 = (len<<16) | 0x6d2b} [V].  Measured live shape: base in device window
0x80000000..0xffffffff, depth-1 = 0x1f, indices <= 0x7ff with a 0x400 wrap-phase bit
(phase42 E.3 [V*]).

**The one host-driver delta this lane produces:** build/luofu-host-618-delta.patch adds
dma-ranges = <0x02000000 0x0 0x80000000 0x80000000 0x0 0x80000000> to both pcie nodes.  The
device-side 0x8xxxxxxx window values (phase42/43 [V*]) resolve to host DRAM, which on luofu
starts at phys 0x80000000 (DTS memory@80500000), so the window is **identity** - and the DWC
framework then programs the RC **inbound** window from dma-ranges
(dw_pcie_iatu_setup -> dw_pcie_prog_inbound_atu, pcie-designware-host.c:945 in the 6.18.60
tree).  The patch is NOT applied to target-skeleton and NOT built: it is a stage-B2 bench
prerequisite only; stage B1 (upload/mailbox/ready) uses only outbound windows.

**Portability trap documented in code:** the vendor's 5.10 tree ran a 2 GiB userspace/kernel split
where host VA == device address; our 6.18 config is **VMSPLIT_3G**, so that identity must NOT be
copied.  The skeleton always derives device-side addresses from dma_alloc_coherent() dma
handles, which are numerically the 0x8xxxxxxx values the ETE engine expects once dma-ranges lands
(and are identical to them even without it, since phys == device addr here - but the RC inbound
window itself still requires the dma-ranges line).

**IRQ wiring (documented gap, not fixed here):** on 6.18 there is no MSI (host msi_init
returns 0) and no INTx demux (radm status/clear are in the never-read misc window); the stock
5.10 numbering was IRQ 207/209 ([V*]).  The skeleton therefore serves D2H with a **polled bitmap
scan** (read-only on BAR0 + the contract drain write); a request_irq() path is compiled in
behind use_irq=1 for when the host side resolves INTx.

## 2. What the skeleton implements (contract -> code)

build/hi5622_pciet.h (contract constants + structs), build/pciet_main.c (module), and
build/pciet_ete.c (rings), all original text.  Mapping:

| vendor function (decoded) | skeleton | notes |
| --- | --- | --- |
| oal_pcie_set_inbound (BAR2 branch) [V] | pciet_prepare_windows() | 6 viewports in the vendor order, then read-back verification (phase18 discipline) |
| firmware_file_send / pcie_write [V] | pciet_fw_upload() | chunked __iowrite32_copy to BAR0+0x6f8000; alias read-back in the verify attr |
| single-shot bal_write(0x40000108, 0x5a5a) [V] | pciet_cpu_release() | explicit sysfs step |
| 0xcece D2H gate [V]/[U] | pciet_d2h_gate_announce() | host write site [U] - own step, logged |
| pcie_ete_init_src/dst_ring [V*] | pciet_ete_ring_alloc/init() | SR0 group A + DR0 group B; enable bits deliberately untouched (vendor order [U]) |
| shuangta_ete_sr_dscr_fill [V] | pciet_ete_sr_send() | node (len<<16)|0x6d2b, producer register bump |
| pcie_msg_send_irq [V] | pciet_msg_send_irq() | mask 0x40039010 -> drain-poll doorbell -> doorbell write; twin bit-0 behind twin_enabled=0 |
| pcie_msg_handle [V] | pciet_d2h_scan() | read bitmap -> drain 0 -> dispatch ids 0..10 through an 8-byte slot table |
| boot table ids 1/2 (.data+0x2660) [V] | pciet_d2h_plat_ready/host_ready | completions + "Device plat ready!" / "DEVICE READY"; id 3 = ETE-done -> DR consume |
| dev_status_check [V*] | pciet_dev_status_show() | 11 read-only words + bitmap/doorbell/ETE-intr |

**Safety model:** probe() writes nothing to the device (only PCI-core enumeration + ioremap +
host-DRAM allocation); every device write is a single explicit sysfs store (prepare, upload,
release, announce, ringinit, send, poll) with dmesg logging; BUILD_BUG_ON asserts no writable
offset collides with the never-write out[5] and a runtime gate sits on the only write helper.
Read-only checkpoints: out1, ready1, ready2, d2h_count, windows, status.

## 3. Build status (what compiles)

hi5622_pciet.ko - 93,468 B, ELF32 ARM, license=GPL, vermagic 6.18.60 SMP mod_unload ARMv7
p2v8, 44 function symbols, **zero warnings with -Werror**, built with
arm-openwrt-linux-muslgnueabi-gcc (14.4.0) against the built 6.18.60 tree.  Build line and fix
log: build/BUILD-NOTES.md.  The .ko lives in the local workdir
/home/shibbityshwab/wifi-transport-port-work/build/ (sha256
87fb532de76507e051685bd540e1f398c0b5e990ac317845fc57a28b02baddd5); only text is committed.

## 4. What remains (transport slice)

1. **Device-validated semantics (bench):** the whole B1/B2 plan above is unexecuted - the
   bitmap/doorbell/twin writes are host-side reproductions of a [V] decode that must be
   device-confirmed attended, per BENCH-PLAN.md.
2. **Twin word (+0x3f1800) [U]:** pinned only by bench; off by default.
3. **ETE enable sequence (+0x00/+0x08/+0x48) [U]:** ring_init programs base/depth/indices only;
   the enable order awaits the B2 session.
4. **RC inbound window:** needs the dma-ranges patch applied + built (host-driver change).
5. **INTx delivery:** blocked by the never-read misc window; polled D2H is the standing design.
6. **hcc message semantics:** the hcc header (driver-re sec 3.1) is decoded but *unused* beyond
   logging; the two wire-header readings (u16@+4 flag/id vs length) are arbitrated by the first
   bench capture - deliberately left as the seam for the hdpp/hmac slices.
7. **DR payload correctness:** node devva-to-VA resolution is table-based; the exact per-channel
   depths (0x1f) and the 0x400 index phase-bit are defaults from [V*] measurements.

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
(about 675 KB).  Sequencing per wifi-framework-port M4: transport (this lane) -> hdpp ->
alg/hal/shuangta -> hmac/wal ko-only.  The closed firmware keeps any full result
upstream-impossible; vendor GPL source remains the fast path, and these numbers are the
cross-check list for whatever source arrives.

## 6. Evidence ledger

| claim | artifact / command | grade |
| --- | --- | --- |
| module builds clean on 6.18.60 | build line + .ko sha256 in build/BUILD-NOTES.md; vermagic read from .modinfo | [V] |
| register/window constants | wifi-driver-re sec 4, wifi-firmware-re sec 2/3, phase18 A.4-A.6, phase42 E.2/E.3 | [V]/[V*] |
| endpoint iATU viewport layout + six regions | phase18/inbound-map.md A.4-A.6 (+ the -0x804 config<->membar shift) | [V]/[V*] |
| doorbell write order / D2H dispatch | objdump decode quoted in wifi-driver-re sec 4.2 | [V] |
| dma-ranges identity + DWC inbound programming | device window 0x8xxxxxxx ([V*]), DTS memory@80500000, pcie-designware-host.c:945 (6.18.60 tree) | [V*]/[I] |
| family byte sizes | census/family-sizes.csv (pyelftools STT_FUNC st_size sums over the two vendor .ko) | [V] |

**Redaction / scope:** no device contact; no vendor .ko/firmware/calibration/INI committed;
the CSVs carry counts and sizes only (function names already live in the framework census).
The module is a host-side skeleton - loading it performs no device writes by construction.
