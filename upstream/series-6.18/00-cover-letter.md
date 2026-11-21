# [PATCH 00/26] ARM: hisilicon: add Hi5671Y "luofu" (Cudy WR3000 v2) platform and driver support

> Submission form for review, not a release.  The target image built from
> this patch stack is now bench-verified (2 CPUs, network through the
> documented mirror workaround, SSH, persistent overlay; 2026-11-21), but
> the series itself has had no upstream review and most drivers remain
> RFC/draft.  Read "Test evidence" and "Hardware validation status" before
> reading any driver as working silicon support.

## What this series is

This is a 26-patch kernel series for the HiSilicon Hi5671Y "luofu" SoC
(2x Cortex-A9, PL310 L2, 128 MiB DDR3) and the Cudy WR3000 v2.0 board
(board R116, 128 MiB NAND, Hi5622V100 Wi-Fi).  The unit ships OpenWrt
22.03.6 on kernel 5.10.201 and its stock U-Boot boots a FIP container from
NAND; this series is the kernel side of a project that is porting that
platform to current Linux and to an upstream OpenWrt target.

The series adds:

- the ARM machine and early-boot support needed to build for the platform
  (0001-0003);
- the documented-minimum drivers and devicetree bindings for the blocks
  whose register/protocol model is currently understood well enough to
  describe (0004-0008, 0011-0014, 0017);
- the CRG clock-tree extension (0019): the 16 gate clocks are parented on
  their decoded vendor-DT input clocks, and the two PLLs and the two muxes
  are registered as read-only clocks with no set_rate/set_parent write
  path.  The PLL rate equation and the mux/gate parents are decoded from
  the vendor hi_clk.ko and the vendor devicetree and have never been
  measured on the board;
- an RFC SMP bring-up (0018) whose CPU1 release is decoded from the stock
  vendor kernel and now verified on the target image: with the board DTS
  carrying the two loader hand-off nodes the release boots 2 CPUs
  (2026-11-21).  The kernel patch alone is not sufficient and remains
  staged separately;
- RFC or partial drivers for the blocks where the hardware model is not
  complete (0006, 0009-0011, 0013, 0015-0017, 0020-0026).  These are
  explicitly marked and several refuse to probe;
- the first draft of the wired data path (0020: PIE ring engine plus the
  GEMAC netdev/phylink front end) and the read-only internal-switch
  baseline probe (0021).  Both are drafts built on the 2026-11-18
  read-only register inventory of the stock vendor system; 0020 is
  fail-closed behind a command-line arm gate that has never been used,
  and 0021 never writes a register at all;
- the staged connectivity follow-ups (0022: LSW W3 power-up plus the
  W4/W5 qid-map write set behind two module parameters, with a
  conformance gate, readback and restore; 0023: GEMAC netdev readiness
  gate, phylink carrier fix and MAC-address plumbing).  Both are drafts:
  every write in 0022 needs gates that have never been set, and 0023
  registers no interface unless the PIE arm gate and both LSW gates pass
  in one attended boot;
- the completion/egress/MAC follow-ups: 0024 decodes the recovered v250
  RX completion descriptor, delivers received frames to the GEMAC netdev
  registered for the completed switch port and builds the full normal
  non-TSO TX descriptor (still inside the never-passed
  `luofu_pie.arm=1` arm path); 0025 adds the gated W4 R1/R3/R5
  probe-configuration write stage to the LSW driver and tightens its
  readiness to require both that configuration and the W2 qid map; 0026
  takes the GEMAC netdev address from the read-only bdinfo MTD nvmem
  cell.  All three are drafts that have never executed on hardware and
  none has been observed by the silicon.

It deliberately does **not** contain: the board DTS, the SMP devicetree
enable-method (0018 is inert without it; it belongs to the attended board
DTS stage), the Wi-Fi driver, a working or boot-tested internal-switch or
GEMAC/PIE data path (0020 is an arm-gated draft that has never run, 0021
only validates measured state read-only, 0022's write gates have never
been set, 0023 registers no interface without them, 0024's decoded
completion/TX descriptor has never been accepted by the engine, 0025's
egress gate has never been set, and 0026's nvmem read has never run), or
any OpenWrt target/image-recipe files.  See "Scope split" and
"Dependencies".

## Target hardware

- SoC: HiSilicon Hi5671Y "luofu" (2x Cortex-A9), vendor bootloader U-Boot
  2022.07 which only boots its own FIP container format.
- Board: Cudy WR3000 v2.0, vendor board name R116.  Note that the retail
  enclosure can say v1: the PCB is the luofu v2 design, not the MediaTek
  MT7981 v1 board.  This identity warning belongs on any device page.
- NAND: ESMT F50L1G41LC-class SPI-NAND behind the FMC controller.
- Wi-Fi: Hi5622V100, vendor-only kernel modules with no open source; the
  Wi-Fi data path is **[X] out of scope** for this series.
- Kernel image contract: the stock loader stages the kernel slot at
  0x83200000 and the uImage carries load/entry 0x80608000; the kernel must
  be built with TEXT_OFFSET 0x00608000 (patches 0002/0003).  This is a
  bootloader dependency that cannot be changed from the kernel side.

## Scope split

**1. Core platform (RFC, UP-first)** - 0001-0003
New arch/arm/mach-hisilicon machine under the existing ARCH_HISI umbrella,
the ARCH_LUOFU Kconfig/text-offset wiring, and the ARM early-boot change
that makes the non-immediate 0x00608000 TEXT_OFFSET assemble.  These
patches are UP-first: the machine descriptor leaves `.smp` unset (patch
0001 describes that state).  SMP bring-up is the separate, attended patch
0018 below and is not part of this mergeable set.

**2. Peripheral drivers and clock tree, ready for review** - 0004,
0005, 0007, 0008, 0012, 0014, 0019
CRG clock/reset, watchdog, MDIO bus, Genius PHY, thermal sensor, the CRG
reset-polarity fix, and the CRG clock-tree extension.  "Ready" means: the
implemented subset is backed by a citable artifact (measured register,
decoded vendor devicetree/module, or a documented software interface),
probe does not write unverified hardware, and known gaps are documented in
the driver.  It does not mean the driver has been validated on hardware -
none has.  0019 is review-ready as a read-only provider extension: it
registers the PLLs and muxes with recalc_rate/is_enabled/get_parent only
(no set_rate, no enable/disable, set_parent returns -EOPNOTSUPP), so the
driver cannot mis-program a clock.  Its values are the caveat: the PLL
rate equation and the mux/gate parents are decoded from the vendor
hi_clk.ko and the vendor devicetree, and the boot-programmed control
words have never been read back on this board.  Treat the decoded rates
and parents as [U] until one attended read-only clk_summary plus
control-word readback.  The CPU mux (0x138 bit 3) must never become
writable: it selects the CPU clock source.

**3. Peripheral drivers, RFC** - 0006, 0011, 0013, 0017
FMC SPI-NAND (critical storage path; ECC/OOB/quad-enable semantics
unverified, and it depends on a v6.19 SPI-NAND chip entry), pinctrl
(consumer/default pin states unmeasured), PCIe host (never enumerated;
write-only misc-window rules only), and the A/B boot selector (new sysfs
ABI; the recorded-pair contract is reconstructed from local evidence - the
bootloader env selects the pair, the SYSCTRL cell is its opposite-pair/failed-
boot record, and a boot-time recorder in the target image is required for
stability - but the protocol is not hardware-validated).

**4. RFC / partial drafts, do not merge** - 0009, 0010, 0015, 0016,
0020-0026
GEMAC (0009: binding/resource half only; probe refuses, no netdev, no
data path), machine restart (0010: provisional CRG reset command), PIE
packet engine (0015: ring scaffolding that refuses to probe), the
internal LSW switch (0016: D1a resource/binding skeleton that refuses to
probe), and the follow-up drafts built on the 2026-11-18 read-only
register inventory and module disassembly of the stock vendor system:

- 0020 replaces the PIE scaffolding with the recovered ring engine and
  adds the GEMAC netdev/phylink front end that forwards transmit into it.
  The data path is fail-closed: probe returns -EOPNOTSUPP before the
  first register write unless `luofu_pie.arm=1` is passed on the kernel
  command line, both devicetree nodes stay disabled, and the armed path
  (the only PIE MMIO write path in the series) has never executed.  0020
  itself does not decode the RX completion (patch 0024 adds that decode),
  so no received frame is handed to the stack, and frames cannot reach a
  wire until the internal-switch egress map exists.
- 0021 turns the LSW skeleton into a read-only D1b baseline probe: it
  validates the measured powered state, 30 measured baseline words and
  both derived qid maps against all 384 measured table words, then
  refuses.  It contains no `writel()`, enables no clock, drives no reset
  and registers no netdev; the values it carries were read from the
  stock vendor system by a fail-closed read-only inventory tool, not by
  the driver.  The attended W3-W5 write campaign (plus the unmodelled
  W7 learning/forwarding check) is still owed.
- 0022 turns the read-only switch probe into the staged write path,
  default-off: with no parameter it is the 0021 read-only probe.  The
  module parameter `luofu_lsw.arm=1` enables the W3 step (both CRG gate
  clocks, the four resets released, the powered state re-read, the
  measured state and both qid maps snapshotted) and still writes no
  switch register; adding `luofu_lsw.qidmap=1` programs the reviewed W2
  qid-map write set for the populated rows (72 table-A words for ports
  4-12, 128 table-B words for ports 40-43, value 0x100 | qid), refuses
  before the first write if an unpopulated qid-map word is non-zero,
  reads every programmed word back and restores the found values on a
  mismatch.  The new `luofu-switch.h` is the readiness contract for the
  GEMAC front end, and the switch binding gains the measured hardware
  port number (`hisilicon,port`).  Neither gate has ever been set and no
  LSW register has been written by this code.
- 0023 makes the GEMAC front end register a netdev only when the whole
  egress path is ready (`luofu_pie_armed()` and
  `luofu_switch_ready()`), so a boot that cannot carry a frame never
  exposes an interface; it also moves the phylink instance to
  `PHYLINK_NETDEV` so the carrier follows the netdev, logs the resolved
  link, and takes the netdev address from the standard devicetree MAC
  properties with a random fallback.  The only MAC register writes are
  still the measured 1 Gbps link-up/link-down words; the TX-to-egress
  mapping remains [U], so no frame is delivered and TX egress is unproven.
- 0024 completes the PIE descriptor model: the recovered v250 RX
  completion descriptor is decoded (length at +0, status/error code at +8
  bits 6:0, port/vport at +16 or the no-status bytes at +23/+24), a good
  frame is unmapped, delivered with `napi_gro_receive()` to the net_device
  that registered the completed port, and the slot re-armed; the full
  normal non-TSO TX descriptor is built (length, 0x74 class word, DMA
  address, port tag, type-0 control words) and the doorbell is the masked
  ring index.  The GEMAC front end reads the now-required
  `hisilicon,port` switch-port property and registers/unregisters its RX
  port around `ndo_open`/`ndo_stop`.  Every PIE register write stays inside
  the existing `luofu_pie.arm=1` arm path, which has never run; the decode
  has never been observed by the engine, the two RX flag classes remain
  semantically [U], the ring-index reading is [I] and the LSW-side egress
  selection is [U].
- 0025 adds the W4 stage that sits between 0022's W3 power step and its W5
  qid-map write: with `luofu_lsw.arm=1 luofu_lsw.egress=1` it programs the
  nine recovered R1/R3/R5 probe-configuration words, refuses before the
  first write unless the read-only `fam+0x504` cell-count conformance
  value and the target words are in the measured state, derives every
  readback expectation from the pre-write snapshot, and restores all found
  values on mismatch.  Readiness is tightened: `luofu_switch_ready()` now
  requires that configuration as well as the qid map, so
  `arm=1 qidmap=1` alone programmes and verifies the map and then refuses;
  `egress`/`qidmap` without `arm=1` is an explicit -EINVAL.  Neither the
  gate nor any of the writes has ever run, and the EQM queue tables,
  classifier/L2/VLAN formats and per-frame egress selection remain [U].
- 0026 takes the GEMAC netdev address from the plaintext factory MAC that
  the board DTS exposes read-only at `bdinfo+0xde00` through a
  fixed-layout/mac-base nvmem cell (LAN = base, WAN = base+1); the probe
  reads it before allocating the net_device, honours `-EPROBE_DEFER` from a
  not-yet-registered MTD nvmem provider instead of randomising, keeps the
  logged random fallback for every other error and installs
  `ndo_set_mac_address`/`ndo_validate_addr` so userspace MAC assignment
  still works.  The DTS wiring and the guarded board.d LAN/WAN map are
  target content, not part of this patch, and the `mac-base` cell
  post-processing is an OpenWrt-carried backport rather than vanilla
  6.18.60.  The read has never run on a booted target, and the patch
  registers nothing unless the 0023 readiness gate passes.

These are included so the design can be reviewed as a whole; they are not
submission candidates until the hardware experiments (one attended PIE
arm-gate session; the LSW W3-W5 campaign; the decode/TX/egress validation
sequence; and one combined PIE+LSW gate boot for 0023) have run.

**5. SMP bring-up (RFC; kernel patch necessary but not sufficient)** - 0018
The CPU1 release (pen word, CRG resume bits, wakeup IPI) is decoded from
the stock vendor kernel's private `hisilicon,hsan_smp` method
(opensource/docs/SMP-RE.md) and registered as the documented devicetree
method `hisilicon,luofu-smp` in a new
arch/arm/mach-hisilicon/platsmp.c.  The method is fail-safe: it stays
inert when the enable-method or either syscon node is absent, and without
the enable-method the platform boots UP.  The target image is
bench-verified with 2 CPUs on 2026-11-21, but the release code was correct
all along: the actual release fix is the board-DTS loader hand-off
contract (a console alias and a flashinfo reserved-memory node, see
SMP-RE.md section 24).  This patch is therefore an RFC for upstream
review, and the DTS series must carry the enable-method plus the two
hand-off nodes; posting 0018 alone would be inert.  The release-line SMP
investigation diagnostics (060/061 etc.) are bench-only and excluded from
the release series (05-release-line.md section 6).

**6. Not-for-submission items (not in this series)**
- the board DTS and loader-match overlay: draft, TODO-heavy, delivered
  through the OpenWrt target's dts/ directory today; an upstream kernel
  DTS patch comes only after the bindings settle and a full-draft
  dtbs_check is clean (see 03-rationale-tree-placement.md);
- the OpenWrt target, image recipe (FIP trailer, squashfs-in-UBI,
  sysupgrade container), base-files/sysupgrade hooks, target config and
  host tools: userspace/OpenWrt tree content, not kernel patches;
- the A/B sysupgrade path and installer: host-only and mocked so far;
- Wi-Fi: [X], blocked on a vendor source/licence answer (GPL request sent,
  no reply);
- the bench/lab kernel edits (module_alloc, JFFS2/UBI hooks, initramfs
  vendor-stack loader, etc.): research instrumentation that must never be
  submitted;
- vendor-derived binaries, blobs, extracted DTBs or calibration data: none
  are in this series; drivers are GPL-2.0 code derived from documented
  reverse engineering of the vendor's GPL binaries (provenance note below).

## Dependencies

- Baseline: pristine linux-6.18.60 (tarball sha256
  3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e).
- The series is ordered: 0001-0003 must precede everything that selects
  ARCH_LUOFU; 0004 (CRG) must precede 0005, 0006, 0007, 0009, 0013, 0015
  and 0016 at the devicetree/clock-reset level; 0014 fixes 0004 and should
  be folded into it before a real submission; 0010 introduces the SYSCTRL
  binding that 0017 also uses.  0018 maps the pen/resume pages through
  the CRG syscon node (0004) and the SYSCTRL syscon node (0010), and stays
  inert if either is missing.  0019 extends the CRG provider added by 0004
  and builds on the 0014 reset-polarity fix; its binding makes seven input
  clocks (clocks/clock-names) required on the CRG node, and the board DTS
  that provides them is OpenWrt-target content, not part of this kernel
  series.  0020-0026 are last; see the bullets below for their ordering
  constraints.
- 0018's devicetree enable-method (`hisilicon,luofu-smp`) is not in this
  series: the board DTS is OpenWrt-target content and is excluded above.
  A DT without the property boots UP (unmatched CPU method) - that is the
  UP-first staging, not a kernel patch change.
- The board DTS is not in the series.  The DTS currently lives in the
  project's OpenWrt target skeleton and consumes the bindings added here;
  a kernel DTS submission is a later series (arch/arm/boot/dts/hisilicon/
  already exists in 6.18, so it is a file+Makefile addition, not a new
  directory).  For 0019 that DTS dependency is explicit: the CRG node must
  carry the seven fixed input clocks and their clock-names (osc, ahb, apb,
  gemacN, lsw_dp, lsw_pfe, efuse_ring) and the two mux-parent clocks; the
  target DTS gets them from the clock-tree lane, and an upstream DTS
  series must add them before the CRG can probe.
- 0020 depends on 0009 and 0015: it rewrites `luofu-gmac.c` and its
  Kconfig entry and YAML binding (0009) and the PIE driver and binding
  (0015), so both must precede it; 0004/0014 provide the CRG clock/reset
  ABI it consumes.  Its ring tables and node enablement are board-DTS
  content, not part of this kernel series.
- 0021 depends on 0016 (it extends the same driver and YAML binding) and
  applies after 0020, which edits the same
  `drivers/net/ethernet/hisilicon/Kconfig` first (it therefore applies
  with a small line offset in that file); 0004/0014 provide the CRG
  gate/reset state it checks read-only.  Its 17-window DTS model, the CRG
  phandle and the port bitmap are board-DTS content, not part of this
  kernel series.
- 0022 depends on 0021 (it extends the same switch driver, register
  header and YAML binding, and its default path is the 0021 probe) and
  builds on 0004/0014 (the CRG clock/reset ABI its W3 step drives).  It
  must follow 0021 and applies after 0020/0021, which edit the same
  `drivers/net/ethernet/hisilicon/Kconfig` file first.  Its switch port
  data (`hisilicon,port`) and the enabled switch/PIE/GMAC nodes are
  board-DTS content, not part of this kernel series.
- 0023 depends on 0020 (it extends `luofu-gmac.c`, its Kconfig entry and
  YAML binding, and calls `luofu_pie_armed()` from 0020) and on 0022
  (`luofu_switch_ready()` and `luofu-switch.h`).  It must follow both
  and applies after 0022, which edits the same ethernet Kconfig file
  first.  Its MAC address may come from the board DTS, and the enabled
  nodes and boot parameters are board-DTS/command-line content, not
  kernel patches.
- 0024 depends on 0020 (it extends `luofu-pie.c`, `luofu-pie.h` and the
  PIE binding) and on 0023 (it extends `luofu-gmac.c` and the GEMAC
  binding, and its RX-port registration only runs when the backend is
  armed and the egress map is ready).  It must follow both and sits after
  0023 in the consolidated order, which is the GEMAC file state it was
  written against.  It makes `hisilicon,port` a required MAC property, so
  the board DTS (out of series) must provide it.  The vendor descriptor
  decode is read-only disassembly evidence, not a patch.
- 0025 depends on 0022 (it extends the switch driver, its register header
  and its Kconfig help, and its default/refusal path is the 0022 code) and
  builds on 0004/0014 for the gated W3 clock/reset step it follows.  It is
  file-disjoint from 0024, so it applies directly after it with no offset.
  Its write set and expectations are the vendor probe shape applied to the
  measured 2026-11-18 state; the EQM tables and the per-frame egress
  selection are outside it.
- 0026 depends on 0023 (it extends `luofu-gmac.c` and the GEMAC binding,
  and its new code runs only after the 0023 registration gate) and on 0024
  (same GEMAC file; 0026 was refreshed against the post-0024 context and
  applies cleanly after it).  Its board DTS nvmem wiring and the guarded
  board.d LAN/WAN map are OpenWrt-target content, and its `mac-base` cell
  parsing is supplied by an OpenWrt-carried backport, not vanilla
  6.18.60 - if those backports change, the WAN +1 offset silently stops
  being applied.  The hardcoded `bdinfo+0xde00` offset is from the local
  flash dump, not from this read path.
- 0020, 0021, 0022 and 0025 encode facts measured by the 2026-11-18
  read-only inventory of the stock vendor system
  (`../../docs/INVENTORY-2026-11-18.md`): the PIE channel/ring
  configuration, the LSW baseline and qid maps, the W2 write model and the
  measured post-boot R1/R3/R5/fam words the 0025 expectations are derived
  from.  That artifact is the dependency for the numbers, and a second
  boot/unit inventory is still owed before the measured baseline can be
  called stable.  0024's completion/TX descriptor layout comes from the
  read-only disassembly of the vendor v250 module (the same inventory's
  ring-pointer values are the other input); 0023 and 0026 carry no new
  measured values.
- The FMC controller (0006) needs the ESMT F50L1G41LC SPI-NAND chip entry,
  which is a v6.19 addition; it is not in 6.18.60 and is currently supplied
  by OpenWrt's generic backport 434.  Do not post 0006 before that entry
  exists upstream, or carry the chip entry in the same series.
- A MAINTAINERS entry is proposed in 02-maintainers-proposal.md.  The
  identity in the Signed-off-by lines and in the binding maintainers:
  fields is the project's git configuration identity
  (ShibbityShwab <18537102+ShibbityShwab@users.noreply.github.com>); DCO
  requires a real legal name before this is sent.
- Provenance: the register models are recovered from the vendor's
  GPL-2.0 kernel modules and devicetree by reverse engineering; no vendor
  code or binary is copied.  A GPL source request to the vendor was sent,
  no reply is held.  Reviewers must be told this explicitly; "clean-room"
  would be inaccurate.

## 2026-11-21 release-line update (release-critical)

- **SMP fix = DTS-only loader hand-off contract, and it is release-critical.**
  The stock U-Boot `bootfip` staging aborts without `/aliases/console` and
  `/reserved-memory/flashinfo@80600000` (`compatible = "hsan,flashinfo_reserved"`,
  `reg = <0x80600000 0x4000>`, `atag-offset = <0x1000>`); with them the
  target image boots 2 CPUs.  This kernel series is necessary but not
  sufficient.  Full record: `05-release-line.md` section 2 and
  `../../docs/SMP-RE.md` section 24.
- **The DTS dependency is an explicit upstream artefact: DTS-0001.**  The
  kernel form is kernel-only, so the board DTS with the two loader hand-off
  nodes ships as
  `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`
  (sha256 `c029fd44...`, current final; adds
  `arch/arm/boot/dts/hisilicon/luofu-r116.dts`, 681 lines).  The DTS is the
  conservative comment-only cleanup plus four individually boot-tested active
  deltas: TWD level-high, MAC-nvmem removal, switch MMIO-first order and
  cudy-compatible ordering; it also carries the stock board_id=3 LED/keys nodes
  and the loader hand-off contract.  The DTS series must state the base-DTB
  8-byte alignment / `pad-dtb` build requirement (compiled DTB is 12,650 B
  unaligned sha256 `a0af2899...`; `Build/luofu-pad-dtb` pads it to
  12,656 B sha256 `01b74561...` - top-level
  `docs/TRAILER-FORMAT.md` section 11).  The superseded active cleanup
  caused a hardware regression (image 6f19c6d6; UART log sha256 15dc3188...);
  every later active delta is boot-tested separately (TWD: image f6685b80;
  final deltas: image c3ef2a50).  See `07-dts-upstream-bindings.md`.  Post
  it together with 0018; see `06-dts-series-dependency.md`.
- **The release line is not this 26-patch review form.**  It is the target
  patch stack (**62 files: 001-058 + 062-065**, no duplicate numbers) minus 11
  bench-only SMP diagnostic patches: 51 patches, classified in
  `release-line/manifest.tsv` and validated host-side by
  `tools/check-release-line.sh` (51/51 apply with **0 fuzz** and
  strict-clean; loader hand-off DTS contract compiles and DTS-0001
  applies/compiles with the same contract).  This review form remains the
  kernel-submission artefact.
- **Bench diagnostics excluded from the release series:** 028, 029, 033,
  034, 035, 037, 039, 062, 063, 064, 065 (the former duplicate 042 became 062
  and its dependent diagnostics moved to 063/064/065).  They are not the SMP
  fix; the release fix is the DTS contract above.
- **Canonical configuration:** no `luofu_smp_*` tokens; built-in cmdline and
  the canonical attended gates are recorded in `05-release-line.md` section 4
  and `../../target-skeleton/README.md`.
- **The earlier "2-CPU hang" is retracted** (host USB-Ethernet outage, not a
  kernel hang; no Oops/panic/BUG/WARNING; the same image passed the soak
  after the link was restored).

## Test evidence (host-only; already recorded)

| # | check | result | evidence |
| --- | --- | --- | --- |
| 1 | sequential apply of the 26 series files to pristine 6.18.60 | patch(1) 26/26 RC=0; git am 26/26 RC=0; resulting trees identical; 0 fuzz, 0 rejects | 04-validation.md, validation/ |
| 2 | offset note | 0008 has two offsets (-75, -12 in drivers/net/phy/{Kconfig,Makefile}) because the source patch was generated against an OpenWrt-patched kernel tree, and 0021 has one 6-line offset in drivers/net/ethernet/hisilicon/Kconfig because 0020 edits that file first; 0022-0026 apply with no offset; all content applies cleanly (3 offset hunks, 0 fuzz, 0 rejects) | 04-validation.md |
| 3 | checkpatch.pl --no-tree (series patch mode, default and --strict) | 0 errors; 58 warnings, all explained (18 MAINTAINERS, 22 binding split, 16 long lines, 2 help paragraphs; plus 6 strict CHECKs) - 0024 and 0026 add one binding-split warning each and 0025 adds none | 04-validation.md, validation/checkpatch-*.log |
| 4 | full kernel compile of the final 0001-0026 series and the intermediate 001-0023/024/025/026 records (PIE-datapath, LSW-init, net, pie-decode, lsw-egress and mac-nvmem lanes) | the pie-decode lane's **fully-clean 001-026 build** (fresh kernel extraction) is prepare RC=0 (1,233 patching-file lines, 0 .rej) and compile RC=0 with **0 errors and 0 compiler warnings** under CONFIG_WERROR=y (plus 37 pre-existing modpost MODULE_DESCRIPTION warnings); zImage **3,282,528 B sha256 08b76747...**, `_text = stext = 0xc0608000`; its 001-024 build is also RC=0/0 warnings (zImage 3,281,400 B sha256 f4a736cf...) and the 001-026 config re-derivation is byte-identical to the committed target config-6.18 at d1af1b2a... (0024-0026 add no symbol).  The lsw-egress lane compiled 024+025 (vmlinux 133,286,956 B sha256 63648e75..., W=1 clean for the three changed drivers; only pre-existing -Wmissing-include-dirs notices) and the mac-nvmem lane compiled 001-023+026 (zImage 3,281,744 B sha256 8443c8c0..., 0 warnings WERROR).  Earlier builds remain current: 001-0020 zImage 3,276,656 B sha256 dfc7b753..., 001-0021 3,279,096 B sha256 96b927d3..., 001-0023 3,281,376 B sha256 5903db04..., 001-0019 clock-tree 3,264,560 B sha256 4456b0ec... and the SMP record; luofu-pie.o/luofu-gmac.o/luofu-switch.o built, PIE/GEMAC/phylink/switch symbols in System.map, CONFIG_PHYLINK resolves y, config deltas symbol-identical and byte-idempotent, TEXT_OFFSET contract 0x80608000 unchanged | SKELETON-SMOKE.md (clock-tree, PIE-datapath, LSW-init, net, PIE-decode, LSW-egress and MAC-nvmem sessions), target-skeleton/docs/port-notes/{clk,pie,lsw,net,pie-decode,lsw-egress,mac-nvmem}/SUMMARY.md |
| 5 | dt_binding_check per new YAML | RC=0 for every binding (dtschema 2026.9; yamllint unavailable, that one lint step skipped); the corrected boot-selector YAML re-checked 2026-11-18; the extended CRG YAML (0019), PIE/GMAC YAML (0020), switch YAML (0021), the switch/GMAC YAML extensions (0022/0023), the GEMAC/PIE YAML decode-and-port extension (0024) and the GEMAC nvmem extension (0026) checked in their lanes (0025 touches no binding); 0018-0026 add no new YAML file | SKELETON-SMOKE.md sessions 5-12, A/B + selector-fix + SMP + clock-tree + PIE-datapath + LSW-init + net + PIE-decode + MAC-nvmem sessions |
| 6 | board DTS compile (not part of this series) | patched-kernel dtc RC=0, zero warnings; 10,664 B sha256 5726f7cd... without the SMP line, 10,710 B sha256 bc599a4e... with the enable-method line (SMP lane); 11,346 B sha256 a31ab53f... with the seven CRG fixed clocks and the SMP line (clock-tree lane); 11,430 B sha256 d3a9dd86... with the PIE ring tables (PIE-datapath lane); 11,516 B sha256 43c1e94b... with the switch windows and port bitmap (LSW-init lane); 11,571 B sha256 7fd33522... with the enabled switch/PIE/GMAC nodes and the hisilicon,port cells (net lane); 11,996 B sha256 e9c1649f... with the bdinfo nvmem cell/wiring (mac-nvmem lane); the final integrated 001-026 DTS is 12,076 B sha256 5386bade..., cpp+dtc RC=0 with 0 stderr bytes, fdtget reads hisilicon,port=8/9/10/11/12 and dt-validate reports only the five pre-existing baseline diagnostics (none naming PIE/GEMAC/nvmem) | SKELETON-SMOKE.md (A/B install + selector-fix + clock-tree + PIE-datapath + LSW-init + net + PIE-decode + MAC-nvmem sessions), port-notes/{smp,clk,pie,lsw,net,pie-decode,mac-nvmem}/SUMMARY.md |
| 7 | kernel config re-derivation | two kernel_oldconfig runs byte-identical (4106877f...), 309 symbols identical to the delivered delta, idempotent (re-run 2026-11-18); 0019 adds no symbol and the 001-0019 re-derivation is byte-idempotent at the same hash; the 001-0020 delta changes only CONFIG_PHYLINK m -> y; 0021 adds no symbol (001-0021 re-derivation symbol-identical/byte-idempotent 76aaafc8...), the 001-0023 config is symbol-identical to the committed one and byte-idempotent (d1af1b2a...) in the net lane, and 0022-0026 add no symbol: the 001-024 re-derivation in the pie-decode lane (kernel .config 4a17f17a..., target config-6.18 d1af1b2a...) and the 001-026 fully-clean run are byte-identical to the committed 001-023 artifact (the mac-nvmem lane independently observed CONFIG_MTD=y, CONFIG_NVMEM=y, CONFIG_NVMEM_LAYOUTS=y already present) | SKELETON-SMOKE.md (A/B install + selector-fix + clock-tree + PIE-datapath + LSW-init + net + PIE-decode + MAC-nvmem sessions) |
| 8 | OpenWrt image chain (not kernel submission scope) | make world RC=0 in an isolated clone, reproducible container | SKELETON-SMOKE.md (image-recipe session) |
| 9 | release-line + DTS-0001 host validation (2026-11-21, final) | target stack minus the 11 bench-only SMP diagnostics (51 patches) applies in order: 51/51 patch(1) RC=0, 0 rejects, 3 offset hunks, **0 fuzz**, strict git apply 51/51; L0 (62 files) also 62/62 with 0 fuzz/strict-clean. The current loader hand-off DTS contract compiles (DTB **12,650 B** sha256 a0af2899..., pad-dtb on a copy to **12,656 B** sha256 01b74561..., contract preserved) and contains `/aliases/console` + `/reserved-memory/flashinfo@80600000` (reg/atag-offset/enable-method checked). **DTS-0001** (sha256 c029fd44...) applies, compiles to the same DTB and is byte-identical to the current single-source target DTS. Conservative step (7ae18f2) compiled byte-identically to the pre-clean DTB (49fe5c47...); all four active deltas were boot-tested (images f6685b80 and c3ef2a50) | 04-validation.md sections 25-26, 05-release-line.md section 9, 06-dts-series-dependency.md, validation/release-line/ |
| 10 | board-feature hardware baseline (not upstream) | images **7f270b8c** (LEDs/keys/watchdog) and quiet **7421bb7a** verified on the unit: 2 CPUs, six `oem:green:*` LEDs writable, `event0` gpio-keys-polled (reset/WPS), `watchdog0` sysfs; thermal unsupported (temp reads 0, vendor DT disables the sensor) | 05-release-line.md section 3, target-skeleton/RELEASE-NOTES.md |
| 11 | hardware (target image, not upstream) | canonical image `974e6ca6` (6,226,222 B) verified on the unit: 2 CPUs (mpidr 80000001, BogoMIPS 3987.86), LAN via the documented mirror workaround, SSH, ubifs overlay, 15-min soak 0 failures, reboot persistent. No Oops/panic/BUG/WARNING. **This is target-image evidence, not an upstream Tested-by** | 05-release-line.md section 3, parent-repo bench logs named there |
| 12 | per-driver hardware validation | partial: platform/storage/SMP/gated-network exercised through the target image; most drivers not individually exercised; no upstream Tested-by | 05-release-line.md section 10 |
| 13 | DTS conservative cleanup + hardware-regression record (host-only, 2026-11-21) | the first active-DTS cleanup (commit 538377c, image 6f19c6d6) **did not boot** (MTD partition scan stalled 0.90 -> 24.55 s, no luofu-reboot/wdt probe, U-Boot watchdog reset, fallback to stock A; UART log sha256 15dc3188...); it was reverted.  Conservative cleanup: only comments removed, 1,329 -> 691 lines, every active node/name/property/reg order/status/alias unchanged.  Proof: its compiled DTB was byte-identical to the known-good DTB (13,041 B sha256 49fe5c47...; padded 13,048 B sha256 402e4f71...), same as known-good image 7421bb7a.  Superseded for DTS purposes by the boot-tested TWD adoption (row 14).  Host checks are necessary but not sufficient | 04-validation.md sections 22-23, 07-dts-upstream-bindings.md, validation/release-line/ |
| 14 | TWD level-high adoption boot-test (2026-11-21, previous DTS-purpose baseline) | measured GICD data on 7421bb7a (ICFGR1 ID29 bits 27:26 = 0b11, PPI config read-only/fixed); standard `IRQ_TYPE_LEVEL_HIGH` delta applied, image **f6685b80** built/installed via the verified telnet installer (PASS; pair b, selector 0x11): 6.18.60, 2 CPUs, `/proc/interrupts` **`GIC-0 29 Level twd`** (was Edge), TWD counts ~100 Hz on both CPUs, ping 20/20 0% loss, no Oops/BUG/panic/RCU stall.  Adopted in commit 5b401c1; superseded for DTS purposes by row 15 | 04-validation.md section 25, 07-dts-upstream-bindings.md section 6.1, ../../docs/incoming/upstream-blockers/06-twd-irq-flags/ |
| 15 | final board deltas adoption boot-test (2026-11-21, current DTS baseline) | image **c3ef2a50** (sha256 c3ef2a50...) built from the TWD DTS with the MAC-nvmem removal, switch MMIO-first order and cudy-compatible order diffs, installed via the verified telnet installer (PASS; pair b, selector 0x11): 6.18.60, 2 CPUs, `/proc/interrupts` `GIC-0 29 Level twd`, `/tmp/sysinfo/board_name = cudy,wr3000-v2`, compatible starts with cudy,wr3000-v2, userspace MACs ...:73/...:74 (nvmem cells removed), br-lan up, ping 15/15 0% loss, no Oops/BUG/panic/RCU stall.  Current final DTS 681 lines sha256 9d96ac31...; DTB 12,650 B sha256 a0af2899..., pad 12,656 B sha256 01b74561...; DTS-0001 sha256 c029fd44...; validator PASS.  Blockers 3/4/5 closed; two remaining | 04-validation.md section 26, 07-dts-upstream-bindings.md section 6.2, ../../docs/incoming/upstream-blockers/ |
| 16 | switch binding MMIO-first adoption into the series (host-only, 2026-11-21) | patch 021's binding YAML reordered to match the boot-tested DTS (15 MMIO windows first, queue/flow DDR last); driver unchanged (by-name lookups); commit message updated; review form re-ran **26/26 patch(1)+git am, identical trees, checkpatch 0 errors / 58 warnings**; release-line validator PASS; release-line manifest refreshed (sha256 1e00b568...).  Only the flashinfo loader binding remains | 04-validation.md section 27, 07-dts-upstream-bindings.md section 6, validation/ |

Evidence tags used in the project: [V] verified from an artifact/measurement,
[I] implementation inference, [U] unmeasured, [X] blocked.  The only [V]
claims above are host-side build/patch/binding results and recorded vendor
register values; every "works on the board" claim remains [U].

## Hardware validation status (updated 2026-11-21)

The target image built from the extended patch stack (this series + the
board-DTS loader hand-off fix + the bench-gated network patches) is
bench-verified: 2 CPUs, network through the documented CPU-copy mirror
workaround, SSH, persistent overlay, a 15-minute soak with 0 failures and
reboot persistence (canonical image `974e6ca6`, see 05-release-line.md).
That is **target-image evidence, not an upstream Tested-by**, and it does
not validate every driver in this review form.  There is no Tested-by in
this series; most drivers have not been individually exercised on silicon,
and the per-driver open hardware questions below all remain.  Known open
hardware questions include: whether the CRG gate geometry is correct, the
decoded CRG PLL rate equation and control-word values, the mux and
gate parents (none of the clock rates or parents has ever been read back
on this board), the FMC ECC/OOB/quad-enable semantics, the pinctrl
default/consumer states,
the PCIe wrapper/link bring-up, the GEMAC/PIE ring and descriptor model
(0024 decodes the RX completion from the vendor disassembly, but the live
length/status/port values, the two flag classes, the TX descriptor
acceptance and the ring-index pointer semantics have never been observed),
the LSW powered-state transition and the W3-W5 write effects (0021 only
validates the measured read-only baseline, 0022's W3/W4/W5 gates and
0025's W4 egress gate have never been set, and the R1/R3/R5 field
semantics, the EQM queue tables and the per-frame egress selection are
[U]), the GEMAC netdev registration behind the combined PIE+LSW gate set
(0023 has never run and 0024's RX delivery and 0026's bdinfo-MAC nvmem
read depend on it), the restart command sequence and selector protocol,
and the SMP pen-word consumer, CRG resume-bit semantics and SCU pre-state
(SMP-RE.md sections 7-10).
Reviewers should treat every driver as an RFC until the corresponding
hardware step in TARGET-DESIGN.md section 10 is done.

## Draft / refusing status (explicit)

| patch | status |
| --- | --- |
| 0009 GEMAC | **PARTIAL**: claims binding, validates resources, then refuses to probe. No netdev, no TX/RX data path, DT nodes disabled. Do not merge as a network driver. |
| 0010 restart | **RFC / PROVISIONAL**: selector save is measured, but the CRG software-reset command pair and its write order/width were never measured. Must not merge without recovery-armed hardware validation. |
| 0015 PIE | **DRAFT**: scaffolding only; probe allocates nothing permanent and returns -EOPNOTSUPP before touching a register. Not mergeable. |
| 0016 LSW | **DRAFT / D1a skeleton**: validates 15 windows, clocks, resets and the switchdev port model, then refuses with -EOPNOTSUPP before any register access. Not mergeable. |
| 0020 PIE datapath + GEMAC front end | **DRAFT / ARM-GATED**: implements the coherent PIE rings, IRQ/NAPI scaffolding, TX producer and the GEMAC netdev/phylink front end, but every PIE MMIO write is behind the `luofu_pie.arm=1` command-line opt-in and probe refuses before the first write otherwise; both DT nodes stay disabled and the armed path has never run. The RX completion decode and the internal-switch egress map are [U], so no frame is delivered. Not mergeable. |
| 0021 LSW baseline probe | **DRAFT / READ-ONLY**: validates the measured powered state, 30 baseline words and all 384 measured qid-map words, contains no register write, enables no clock, drives no reset, registers no netdev, and still refuses probe. The attended W3-W5 write campaign (plus the unmodelled W7 check) is owed. Not mergeable. |
| 0022 LSW gated write path | **DRAFT / GATED**: keeps the 0021 read-only default. `luofu_lsw.arm=1` enables the W3 power step (two CRG gate clocks, four resets, powered-state re-read, read-only snapshot); adding `luofu_lsw.qidmap=1` programs the reviewed W2 qid-map write set (72 table-A + 128 table-B words of the derived 1..104 qid model) behind a pre-write conformance gate, reads every word back and restores the found values on mismatch. Neither gate has ever been set, no LSW register has been written by this code, and the W3 safety/W4-W5 effect/EQM/classifier/VLAN formats are [U]. Not mergeable. |
| 0023 GEMAC netdev readiness | **DRAFT / READINESS-GATED**: registers a netdev only when the PIE backend is armed and the LSW egress qid map is ready; fixes the phylink type so the carrier follows the netdev, logs the resolved link, and takes the address from the devicetree or a random fallback. Only the measured 1 Gbps link-up/link-down MAC words are written; the TX-to-egress mapping is [U], so no frame is proven to leave a port. Registration has never run. Not mergeable. |
| 0024 PIE completion decode + TX descriptor | **DRAFT / ARM-GATED**: decodes the recovered v250 RX completion descriptor, delivers a good frame to the net_device registered for the completed switch port (`luofu_pie_rx_register()`, port table under a spinlock) and builds the full normal non-TSO TX descriptor tagged with the now-required `hisilicon,port`; every write stays inside the never-passed `luofu_pie.arm=1` arm path. The decode has never been observed by the engine, the live field values and the two RX flag classes are unmeasured/unknown, the ring-index reading is [I], and the LSW egress selection is [U]. Not mergeable. |
| 0025 LSW W4 egress stage | **DRAFT / GATED**: requires `luofu_lsw.arm=1` and adds `luofu_lsw.egress=1`, which programs the nine R1/R3/R5 probe-configuration words after a read-only EQM/word-stability/fam conformance gate, derives every readback expectation from the pre-write snapshot and restores the found values on mismatch; `luofu_switch_ready()` now needs both that configuration and the W2 qid map. `egress`/`qidmap` without `arm=1` is -EINVAL. The gate has never been set, no switch register has been written by this code, and the field semantics, EQM tables and per-frame egress mapping are [U]. Not mergeable. |
| 0026 GEMAC MAC source | **DRAFT / READINESS-GATED**: reads the netdev address from the read-only bdinfo MTD nvmem cell (`fixed-layout`/`mac-base`, LAN = base, WAN = base+1) before allocating the net_device, defers probe on `-EPROBE_DEFER` instead of randomising, keeps the logged random fallback otherwise and installs `ndo_set_mac_address`/`ndo_validate_addr` for the existing userspace path. No register or flash write; the DTS wiring and board.d map are target content, and the `mac-base` parsing is an OpenWrt backport. The read has never run on a booted target and the netdev registration still requires the 0023 gate. Not mergeable. |
| 0013 PCIe | **RFC**: encodes the measured hard access rules (never read the misc window, aligned dwords only) but has never enumerated an endpoint. |
| 0017 selector | **RFC**: new sysfs ABI. The recorded-pair contract is reconstructed from local evidence (docs/SELECTOR-0x21.md: env bootflag selects, the cell records, 0x21 is the project's bench value and never persists) and the store now does the loader-style low-byte read-modify-write; post-boot stability depends on a boot-time recorder in the target image (not this series), and the env re-stamp and the cell's upper 24 bits remain unmeasured. ABI must be reviewed before freezing. |
| 0006 FMC | **RFC**: storage path is critical and its ECC/OOB/quad-enable semantics are unverified. |
| 0011 pinctrl | **RFC**: probe writes nothing and consumer states are unmeasured; applying guessed muxes could break console/NAND. |
| 0018 SMP | **RFC / NEEDS THE DTS CONTRACT**: decoded vendor pen/resume/IPI CPU1 release in a new platsmp.c. The target image is bench-verified with 2 CPUs (2026-11-21), but 0018 alone is not sufficient: the board DTS must also carry `/aliases/console` and `/reserved-memory/flashinfo@80600000` (release-critical; SMP-RE.md section 24). Register values are driver data, no new binding; the SMP investigation diagnostics are bench-only and excluded from the release line. |
| 0019 clock tree | **READ-ONLY / DECODED**: registers the CRG PLLs and muxes with read-only ops (no set_rate, set_parent refused) and parents the gates on the decoded vendor-DT input clocks. The PLL rate equation, field layout and mux/gate parents are decoded from the vendor hi_clk.ko and devicetree; the on-board control words, rates and parents have never been measured. No new Kconfig symbol; the binding extension requires seven input clocks from the (out-of-series) board DTS. |

## Recommended submission sequence

Do not post this consolidated 26-patch file set as one series.  It is
consolidated here so the whole design can be reviewed.  A real submission
splits by subsystem and by readiness:

1. **RFC first, for direction (arm-soc + Russell King):** 0001-0003.
   Ask whether a new arch/arm/mach-hisilicon directory under ARCH_HISI,
   UP-only, is acceptable, and whether the TEXT_OFFSET change (0003)
   should be a separate generic ARM improvement.  No DTS yet.
2. **Ready-for-review series, small and independent:** 0004 (with 0014
   folded in, and 0019 as the immediate clock-tree follow-up - or folded
   in as well) -> clk/reset maintainers; 0005 -> watchdog; 0007 -> netdev
   MDIO; 0008 -> netdev PHY; 0012 -> thermal.  Each with its binding
   split out and its Kconfig/Makefile patch.  Send the CRG provider before
   its consumers, and do not post 0019 before 0004.
3. **RFC series with an explicit open-work statement:** 0011 pinctrl,
   0006 FMC (only after the v6.19 ESMT chip entry lands), 0013 PCIe,
   0010 restart + 0017 selector (needs the sysupgrade protocol story and the
   target's boot-time pair recorder).
4. **Do not send yet:** 0009 GEMAC, 0015 PIE, 0016 LSW, and their
   follow-up drafts 0020 (PIE datapath + GEMAC front end), 0021 (LSW
   read-only baseline probe), 0022 (gated LSW W3/W4/W5 write path),
   0023 (GEMAC netdev readiness gate), 0024 (PIE completion decode + TX
   descriptor), 0025 (gated LSW W4 egress/init stage) and 0026 (bdinfo-MAC
   nvmem source).  They are refusing/partial drafts; post as RFC only when
   asking for design direction, clearly marked `[RFC PATCH]`, and never as
   a working Ethernet/switch port.  0020 is additionally blocked on the
   attended arm-gate session, 0021 on the LSW W3-W5 write campaign, 0022
   on the same campaign (its write gates have never been set), 0023 on one
   combined PIE-arm + full LSW-gate attended boot, 0024 on the attended
   RX/TX decode validation, 0025 on the W4 egress stage (which is also a
   prerequisite for 0023's "both gates pass" readiness) and 0026 on a boot
   that reaches its nvmem read path.  0021 is read-only, so a reviewer can
   inspect it without any write risk; 0022's default path and 0025's
   default path are the same read-only probe, and 0023/0024/0026 register
   nothing until every gate is set.
5. **SMP (0018) as RFC, together with DTS-0001:** the target image is
   bench-verified with 2 CPUs (2026-11-21), so the attended gate is closed,
   but the patch alone is inert.  Post 0018 as RFC together with the
   commit-ready DTS-series entry
   `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`
   (DTS-0001), which carries the enable-method and the two loader hand-off
   nodes (`/aliases/console` and `/reserved-memory/flashinfo@80600000`
   with `atag-offset = <0x1000>`).  Do not post the bench diagnostics
   (064/065 etc.) as the SMP fix.  See `06-dts-series-dependency.md`.
6. **After the bindings are accepted:** the board DTS to
   arch/arm/boot/dts/hisilicon/ (with the stock partition map and the
   identity warning; the SMP enable-method only after S2), then the
   OpenWrt target PR (target/subtarget, image recipe, sysupgrade metadata,
   board files, device page).

Submission order must be dependency order: platform -> CRG -> consumers.
Patch 0014 should disappear as a separate patch (fold into 0004) because
it fixes code first added in this series; keep it only while the series is
carried as an OpenWrt target patch stack.

## Known review items to fix before sending

1. **Split bindings out of driver patches.** checkpatch warns 22 times
   that DT binding docs/includes should be a separate patch.  One binding
   patch per driver, before its driver patch; the dt-bindings clock header
   and binding hunks of 0004/0019, and the PIE/GMAC/switch binding hunks of
   0020-0026, also go with their bindings (0024/0026 extend the existing
   PIE/GMAC YAMLs; 0025 has no binding hunk).
2. **Add the MAINTAINERS entry** (02-maintainers-proposal.md); checkpatch
   warns 18 times about new files without it (0018 adds platsmp.c, 0020
   adds luofu-pie.h, 0021 adds luofu-switch-regs.h and 0022 adds
   luofu-switch.h; 0023-0026 add no file).
3. **Mechanical checkpatch cleanups:** wrap the 16 over-long CRG clock
   table lines (112 > 100 columns), add comments to the spinlock_t
   declarations (the two in 0004 plus the two 0019 re-adds when it rewrites
   the structs), fix two continuation-line alignments in luofu-switch.c,
   and expand the LUOFU_MDIO / LUOFU_PHY Kconfig help text to at least
   four lines.
4. **Fold 0014 into 0004** (and decide whether 0019 is folded in or
   posted as the follow-up clock-tree patch) and **regenerate 0008 against
   mainline** (it currently applies with -75/-12 offsets because it was
   generated against an OpenWrt-patched kernel tree).
5. **Naming consistency:** LUOFU_FMC sits in drivers/spi and would be
   expected to be SPI_LUOFU_FMC; decide before the spi maintainers see it.
   The thermal driver file is luofu_tvsensor.c (underscore) while most new
   drivers use hyphens; follow the subsystem preference.
6. **DCO/provenance:** real legal name in Signed-off-by; a short
   provenance paragraph in the cover letter of the first driver series
   (GPL-2.0, derived from reverse engineering of the vendor's GPL
   binaries; vendor GPL source request unanswered).
7. **State the Wi-Fi scope** in every platform submission: vendor-only,
   blocked, not part of the port.
8. **State the loader dependency** (FIP container, 0x80608000,
   TEXT_OFFSET 0x00608000) and that installation is out of scope until a
   first-install path is validated.
9. **SMP (0018) review answers:** why the pen offset (0xc00), the resume
   offset (0x38) and the resume bit numbers are driver data rather than DT
   properties (vendor-private values, reuse of the existing syscon nodes,
   no new binding surface - SMP-RE.md section 5.5); no CPU hotplug
   (`CONFIG_HOTPLUG_CPU` stays off, matching the vendor ops table); and
   the method string is `hisilicon,luofu-smp`, not the vendor's
   `hisilicon,hsan_smp`.
10. **PIE/LSW (0020-0023) review answers:** the PIE MMIO write path is
   reachable only through the `luofu_pie.arm=1` command-line opt-in, the
   probe refuses before the first write otherwise, and both devicetree
   nodes stay disabled; the ring tables are binding arrays of measured
   values, not board policy; the RX completion status/length decode is
   still [U] and no frame is delivered.  The LSW probe is read-only, its
   baseline values are facts of one measured vendor boot (not yet a stable
   platform property), the two DDR windows are engine-consumed memory and
   not register pages, and the qid-map write geometry is documented but
   not executed.  0022 keeps that read-only default and gates every write
   behind `luofu_lsw.arm=1` / `luofu_lsw.qidmap=1`: the write set is
   exactly the recovered W2 qid-map rows, the pre-write conformance gate
   refuses on any non-zero unpopulated word, and every programmed word is
   read back and restored on mismatch.  The new `hisilicon,port` binding
   property is measured board data (switch ports 9..12 for the four live
   MACs), not an ordering index.  0023 is a readiness contract, not a data
   path: it registers nothing unless `luofu_pie_armed()` and
   `luofu_switch_ready()` both hold, uses `PHYLINK_NETDEV` so the
   carrier follows the netdev, and keeps the only MAC writes at the
   measured 1 Gbps words.  The MAINTAINERS patterns must cover the new
   `luofu-pie.h`, `luofu-switch-regs.h` and `luofu-switch.h`
   headers.
11. **Completion/egress/MAC (0024/0025/0026) review answers:** the RX
   completion decode is a direct transcription of the vendor v250 code
   paths (return value of the vendor helper is not needed, so the
   `napi_gro_receive()` result is deliberately not compared - an earlier
   form compared it against the non-existent `GRO_DROP`), the two RX
   flag classes are recorded as [U] instead of guessed, and the netdev
   port registration makes an unregistered/down port a drop, not a leak;
   `hisilicon,port` is measured board data (vendor switch ports 8..12),
   not an ordering index, and it is required because a MAC without it can
   neither egress nor receive correctly.  0025's expectations are computed
   from the pre-write snapshot with the vendor formulas rather than
   hardcoded (so the readback tests the write path), the `fam+0x504`
   word is read-only because the vendor only lowers it, and readiness is
   deliberately tightened so a boot with both write gates cannot arm a
   data path whose probe configuration was never written; the EQM queue
   tables and the per-frame egress selection are explicitly out of scope
   and stay [U].  0026 reads factory data only: the MTD nvmem provider is
   read-only and root-only with no write callback, the partition is marked
   read-only, `-EPROBE_DEFER` is honoured rather than racing the MTD
   probe, and the userspace provisioning path is kept for DTBs without the
   kernel wiring; the `mac-base` parsing dependency on the OpenWrt
   backports is stated rather than assumed.

## Where the evidence lives

- This directory: 00-cover-letter.md (this file), 01-commit-messages.md
  (per-patch messages + mapping table), 02-maintainers-proposal.md,
  03-rationale-tree-placement.md, 04-validation.md, 05-release-line.md,
  06-dts-series-dependency.md, README.md, commit-messages/, series/,
  release-line/, dts-series/, tools/, validation/.
- Release status for the target: `../../target-skeleton/RELEASE-NOTES.md`
  (canonical 2-CPU baseline, working-LAN workaround, retraction).
- Recorded validation sessions: opensource/docs/SKELETON-SMOKE.md
  (third to twelfth sessions, image-recipe, A/B, selector-fix, SMP,
  clock-tree, PIE-datapath, LSW-init and net sessions),
  opensource/docs/TARGET-DESIGN.md sections 4-7 and 9-12,
  opensource/docs/SMP-RE.md (SMP decode and staging),
  opensource/docs/PIE-DATAPATH.md and opensource/docs/LSW-SPEC.md
  (register/data-path models; PIE-DATAPATH sections 5.2/6 hold the decoded
  completion/TX descriptor tables, LSW-SPEC section 7.8 the egress model),
  opensource/docs/INVENTORY-2026-11-18.md (the measured baseline that
  0020-0022/0025 embed),
  opensource/target-skeleton/docs/port-notes/*/SUMMARY.md (net lane: the
  connectivity gate model and the W3/W4/W5 attend steps; pie-decode lane:
  the descriptor decode and its attended RX/TX checks; lsw-egress lane:
  the R1/R3/R5 write set, conformance gate and attended sequence;
  mac-nvmem lane: the nvmem wiring, guarded board.d map and host tests).
- Current live state and the publication boundary:
  opensource/docs/PUBLISH-READINESS.md (NOT READY: vendor .o history
  purge, GPL reply, DCO/licence files still open).  This series does not
  change that status.
