# incoming/pcie - PCIe host controller (patch 013) summary

**Task:** task-21 (parallel driver lanes, agent `driver-pcie`). **Date:** 2026-11-18.
**Scope:** implement the HiSilicon Hi5671Y "luofu" PCIe root complexes as a
mainline DesignWare host driver, with its YAML binding, Kconfig/Makefile entry,
config symbol and board devicetree nodes via `shared.patch`.  v1 is **two host
controllers and the enumeration path only**: clocks/resets, SoC wrapper RC-mode
and LTSSM-enable stores, outbound iATU windows derived from DT `ranges`, a
configuration-window accessor, DWC link handling and `pci_host_probe()`.
The PCIe Wi-Fi endpoints stay **[X]** (no mainline driver, closed firmware):
their devicetree nodes are documented but disabled.  The RC nodes also stay
disabled because the device write path has never been measured on the board.
**No device was touched** (no router, UART, TFTP, flash or bench tool).
Host-only, evidence-graded.

This directory is an **incoming** deliverable: it carries the new kernel patch
(already placed in `patches-6.18/`) plus the shared-file edits as
`shared.patch`, which the lead applies.  The lane did not edit any shared
repository file.

---

## 1. Deliverables

### 1.1 New kernel patch (already in the repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/013-arm-pci-luofu-host.patch` | `73c413dda1463842aa4bfda63879147894930c64ef6e6909b7914110c9d01202` |

Numbering: `013` follows the merged `001`-`012` (patch `014`, the CRG reset
polarity fix, is the lead's follow-up and is not part of this lane).  The patch
is a bare unified diff (`--- a/...` / `+++ b/...`, no `diff --git`) in the
style of `004`-`012` and applies with `patch -p1` in the OpenWrt
platform-patch order.

`013` adds:

- `drivers/pci/controller/dwc/pcie-luofu.c` (new, 467 lines): the DWC host glue
  driver - CRG clock + four resets, write-only SoC wrapper stores, aligned-dword
  DBI accessors, a configuration-window `pci_ops` for the measured topology,
  CFG0 iATU viewport programming, LTSSM start and link-state reporting,
  `pci_host_probe()`;
- `drivers/pci/controller/dwc/{Kconfig,Makefile}`: `CONFIG_PCIE_LUOFU_HOST`
  (`bool`, `depends on ARCH_LUOFU || COMPILE_TEST`, `OF`, `PCI_MSI`,
  `select PCIE_DW_HOST`);
- `Documentation/devicetree/bindings/pci/hisilicon,luofu-pcie.yaml` (new, 126
  lines): the root-complex binding (reg-names `dbi`/`misc`/`config`, `ranges`,
  `bus-range`, one `pcie` clock, four named resets, optional radm/linkdown
  interrupts), built on `/schemas/pci/pci-host-bridge.yaml` so the standard
  PCI-bus properties and the `pciVVVV,DDDD` child-device rules apply.

### 1.2 `shared.patch` (apply to the repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/docs/incoming/pcie/shared.patch` | `ae14b71debbfb24c2934ab27b102b3d51e5cdd0e0f05f8da99abd6420b69df9c` |

Files and what they carry:

- `target-skeleton/dts/luofu-r116.dts`: rewrites the two disabled PCIe
  placeholder nodes to the final binding shape:
  - `reg` becomes `dbi` + `misc` + `config` (the 4 KiB configuration window is
    the `config` resource the DWC host core requires; `mem`/`io` are expressed
    by `ranges`, as for every mainline PCI host bridge);
  - `ranges` is fixed: the old third entry was mis-typed (0x03000000 = 64-bit
    memory pointing at the config window) and would have consumed an iATU
    viewport; the corrected entry list is the measured MEM window
    (`0x40000000/0x2000000`) and IO window (`0x48000000/0x800000`) per root
    complex (RC1: `0x58000000`, `0x60000000`);
  - `clocks`/`clock-names` (`pcie`), `resets`/`reset-names`
    (`apb`/`pcs`/`phy`/`ctrl`, the measured four CRG lines in the recorded
    vendor deassert order), `num-lanes = <1>` (measured Link Capabilities x1);
  - the endpoint functions are documented as disabled child nodes
    `wifi@0,0 { compatible = "pci59e7,0005"; ... status = "disabled"; }` under
    each root complex (the standard `pciVVVV,DDDD` child-device form; their
    driver is [X]);
  - the root-complex nodes remain `status = "disabled"`.
- `target-skeleton/target/linux/hisilicon/luofu/config-6.18`: adds
  `CONFIG_PCIE_LUOFU_HOST=y` and the six select-driven symbols the canonical
  re-derivation emits (`CONFIG_PCIE_DW=y`, `CONFIG_PCIE_DW_HOST=y`,
  `CONFIG_PCI_ECAM=y`, `CONFIG_PCI_HOST_COMMON=y`, `CONFIG_IRQ_MSI_LIB=y`,
  `# CONFIG_PCIE_DW_DEBUGFS is not set`), each with an `[N]` provenance
  comment.

Post-apply hashes (sanity check for the lead):

- `target-skeleton/dts/luofu-r116.dts` = `683b0e61fe213e4e432e4272350fb0f081e1196d37ad5cb3921aad8a22e72bed`
- `target-skeleton/target/linux/hisilicon/luofu/config-6.18` = `b3ed2a0fb5ba50299cec7af9fd87d15d11d7e07ac8f7df94dea78c3a7fa8db85`

The two files in the repository already match these hashes: the lead applied
`shared.patch` while this lane was still validating, so the repository state is
the post-apply state (verified by hash; `shared.patch` was re-validated against
the pre-apply state copies under the wave directory).

---

## 2. What the driver implements

`pcie-luofu.c` is a DWC host-controller glue driver
(`drivers/pci/controller/dwc/`, using `pcie-designware-host.c` and
`pcie-designware.c`), one platform device per root complex:

- **clocks/resets:** one CRG gate clock (`pcie`) and four named CRG resets
  (`apb`, `pcs`, `phy`, `ctrl`), deasserted in the recorded vendor order
  apb -> pcs -> phy -> ctrl with a short inter-step delay;
- **SoC wrapper (misc) stores:** `misc+0x00 = 0x40000000` (root-complex mode),
  `misc+0x1c = BIT(13)` (iATU enable) before the framework programs the iATU,
  and `misc+0x1c = BIT(13)|BIT(11)` (iATU enable kept, LTSSM enable) in
  `start_link`.  Every misc access is a full 32-bit store with a host-computed
  value; the driver contains **no read of the misc window** (checked);
- **aligned-dword DBI accessors:** the DWC framework's `.read_dbi`/`.write_dbi`
  ops fetch the containing 4-byte-aligned dword and move byte lanes in
  software, so every DBI access the framework issues (config-header words,
  link registers, iATU registers) is one aligned 32-bit access;
- **configuration window:** the bridge's `pci_ops` maps root bus / device 0 /
  function 0 to the measured CFG0 iATU window and uses
  `pci_generic_config_read32`/`pci_generic_config_write32`, which read/modify
  one aligned dword and do the sub-dword selection/merge in software.  The
  CFG0 viewport (index 0) is pointed at the measured target 0 and re-programmed
  lazily for each configuration access; other buses/functions are reported
  absent (the measured topology has none);
- **memory/IO windows:** the framework's `dw_pcie_iatu_setup()` programs the
  outbound viewports 1 and 2 from the DT `ranges` (base=CPU address,
  target=PCI address, 1:1 on this board), matching the measured vendor table;
- **link handling:** standard DWC link wait through the DBI port-logic
  `DEBUG1` link-up/in-training bits, plus a post-enumeration report of the
  LTSSM state (`DEBUG0`), `DEBUG1`, Link Capabilities and Link Status - all
  dword-aligned DBI reads;
- **enumeration:** `pci_host_probe()` scans the root bus, assigns resources
  from the DT windows and adds the devices (the endpoint is expected at
  `0000:00:00.0` / `0001:00:00.0`, matching the measured live vendor system);
- **no MSI controller:** the DWC MSI-controller interrupt line is not described
  anywhere in the vendor devicetree, so a stub `msi_init` keeps the framework
  from claiming the `radm` interrupt as its MSI line; devices are enumerated
  without interrupts (the endpoint driver is [X]).

## 3. Design decisions (and why)

1. **DWC host framework, not a standalone host controller.**  The measured
   register geometry is a pre-4.80 DWC core: iATU viewport at DBI+0x900 with
   the 0x904..0x91c region file, Link Capabilities at 0x07c and Link Status at
   0x082, `DEBUG0/DEBUG1` at 0x728/0x72c, DBI RO-write at 0x8bc.  Reusing
   `pcie-designware-host.c` gives mainline iATU, link and enumeration handling
   for free; the only DWC-specific pieces the SoC needs are the wrapper stores
   and the configuration-window access class.
2. **Configuration window instead of ECAM.**  The root complex has a dedicated
   4 KiB `config` window (measured: CPU 0x50000000 -> PCI target 0 for RC0,
   0x68000000 -> 0 for RC1), so ECAM cannot be used.  The DWC framework's
   non-ECAM mode (per-access CFG0 iATU programming + a 4 KiB window) fits
   exactly; because the window only answers aligned dwords, the
   software-merge 32-bit accessors are used.
3. **Measured topology only.**  The vendor tree enumerates the Wi-Fi endpoints
   at bus 0 / device 0 / function 0 of each domain (i.e. the root complex's own
   DBI config header is not exposed as a PCI function).  v1 therefore serves
   exactly that function from a statically-mapped CFG0 viewport and reports
   every other bus/device/function as absent; general multi-function/deeper-bus
   enumeration is a documented TODO because it needs per-access iATU
   re-programming whose interaction with the wrapper iATU-enable bit is
   unverified.
4. **Write-only misc window.**  The measured hard rule is that the misc window
   must never be read (a read of 0x10161000 panicked the board).  The driver
   maps it only to write the recorded wrapper words; a compile-time assertion
   keeps the offsets in the aligned-dword class, and a static scan proves there
   is no `read[lwb]` against the misc base.
5. **The wrapper iATU-enable bit is kept asserted.**  The recorded vendor order
   enables the iATU, programs the windows, then "clears" the iATU-enable bit
   while enabling the LTSSM.  Because this port re-programs the CFG0 viewport
   lazily at every configuration access, `start_link` writes both documented
   enable bits (0x2800).  This is an inference ([I]) about the wrapper
   semantics, not a measurement, and is one of the first hardware questions;
   see TODO 4.
6. **The reset polarity is fixed in patch 014.**  The four PCIe reset lines are
   measured active-low: 0x14880034 reads 0x000ff309 with both root complexes
   up, i.e. the bits are SET while the lines are released.  The first CRG
   driver revision had assert=set/deassert=clear, which would have driven the
   root complex into reset on probe; the lead's patch 014 corrects the provider
   (assert clears, deassert sets, status inverted).  This driver uses only the
   reset API and relies on the corrected provider.
7. **Nodes stay disabled.**  The device write path (wrapper mode/LTSSM stores,
   DBI writes, iATU programming) has never been measured on this board, so the
   root-complex nodes remain `status = "disabled"` exactly as the port plan
   requires.  Enabling them is a one-line change for an attended hardware run,
   after which the endpoint children stay disabled (their driver is [X]).
8. **Not modelled at all** (each with a reason, see section 9): endpoint
   power/reset GPIOs, radm/linkdown interrupt handling, MSI/INTx routing,
   `dma-ranges`/inbound windows, ACP cache-attribute programming, ASPM/L1SS and
   link-speed changes, and ACPI (not applicable).

## 4. Evidence grading

| claim | marker | evidence |
| --- | --- | --- |
| two DWC root complexes, five windows each, addresses/sizes | [V] | pinned DTS 0x10160000/0x10164000 block; vendor-dt-notes.md section 5; lab luofu-pcie README |
| outbound iATU plan (viewport 0 CFG0 -> target 0; 1 MEM; 2 IO, 1:1) | [V] | transcribed vendor `iatu_rc` table (pinned DTS :1216/:1240) and the iATU selector read-back in the arm-B4/B5 receipts |
| 4 KiB config window, non-ECAM; endpoint at bus 0/dev 0 | [V] | CFG window reads of the endpoint identity (0x59e7/0x0005) on both domains; lspci addresses 0000:00:00.0 / 0001:00:00.0 |
| DBI/cfg only answer aligned 32-bit accesses; misc is write-only | [V] | two recorded live panics (readl and readw at dbi+0x082) and the misc-window panic; aligned-dword accessor receipts |
| CRG clock gate + four active-low resets, order apb/pcs/phy/ctrl | [V] | CRG patch 004; live census of 0x34 = 0x000ff309 with both RCs up; vendor probe order |
| wrapper RC-mode store 0x40000000 and iATU/LTSSM enable bits | [V] recorded values / [U] semantics | vendor `hi_pcie.ko` RE (hi_pcie_set_mode/set_iatu/enable_ltssm order); no store to the wrapper has ever been measured |
| driver code, binding, Kconfig, config delta | [V] | this lane's prepare/compile/dt_binding_check/config re-derivation receipts |
| driver enumeration works, link comes up, endpoints bind | [U] | never run on hardware |
| MSI/INTx route, endpoint power GPIOs, ACP coherency, ASPM/L1SS | [U] | not described by the vendor devicetree/held by no measurement |
| Wi-Fi endpoint driver + firmware | [X] | vendor-only module + closed firmware; out of scope |

## 5. Validation (host-only, isolated clone)

All evidence is under the local gitignored directory
`build/linux-kbuild/waves/2026-11-18-pcie/`; the clone is
`/home/shibbityshwab/openwrt-lab-pcie` (copy of the shared clone, see
`00-env.txt`).  The shared clone was **not** modified.

### 5.1 Series integration

- pristine `linux-6.18.60` dry-run of the series `001-013`: **RC=0**
  (`pristine-dryrun.log`, `pristine-dryrun.rc`).  `014` is excluded on
  purpose: it patches a file created by `004`, so only the sequential prepare
  can validate it (the lead's combined run covers it);
- `make target/linux/prepare V=s` in the isolated clone: **RC=0**, 1,183
  "patching file" lines, 0 `.rej`/`.orig` files; 013 applied last
  (`prepare.log`, `prepare.rc`).

### 5.2 Full compile

- `make target/linux/compile V=s -j16`: **RC=0**, 1m56s, **0** `warning:`
  lines, `CONFIG_WERROR=y` in the resolved config;
- zImage **3,262,016 B**, sha256
  `392d58614d7724d0ac7d4c4c7dfee1f826c9e3ba40860666f26c12d241bbc19d`;
- driver built-in: `drivers/pci/controller/dwc/pcie-luofu.o` (33,036 B);
  `System.map` carries `luofu_pcie_probe`, `luofu_pcie_map_bus`,
  `luofu_pcie_read_dbi`, `luofu_pcie_write_dbi`, `luofu_pcie_start_link`,
  `luofu_pcie_host_init`, `luofu_pcie_pci_ops`, `luofu_pcie_dw_ops`, ...;
- TEXT_OFFSET contract unchanged: `arch/arm/Makefile:175
  textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000`, the build's head objects carry
  `-DTEXT_OFFSET=0x00608000`, and `_text = stext = 0xc0608000`;
- resolved `.config` carries `CONFIG_PCIE_LUOFU_HOST=y`, `CONFIG_PCIE_DW=y`,
  `CONFIG_PCIE_DW_HOST=y`, `CONFIG_PCI_ECAM=y`, `CONFIG_PCI_HOST_COMMON=y`,
  `CONFIG_IRQ_MSI_LIB=y`, `# CONFIG_PCIE_DW_DEBUGFS is not set`.

### 5.3 Devicetree binding

- `make ARCH=arm dt_binding_check DT_SCHEMA_FILES=Documentation/devicetree/bindings/pci/hisilicon,luofu-pcie.yaml`:
  **RC=0**; `DTEX`/`DTC [C]` for the example pass (`30-dt-binding-check.log`).
  dtschema 2026.9; `yamllint` is not installed, so that one lint step is
  skipped (as in earlier lanes).

### 5.4 Board devicetree

- current repository DTS: `dtc` **RC=0, zero warnings**, DTB 9,795 B sha256
  `b5b99db570534eca37d4588d07e62525df2563f0a9780aa9d394b2cb636a2b42`;
- post-`shared.patch` DTS: `dtc` **RC=0, zero warnings**, DTB 10,133 B sha256
  `46fac7ad56c16fde6958ff2415f77bd963686f9ab990af15da128d28f12478fd`;
- `dt-validate` on the integrated DTB: **RC=0**, no diagnostic names a PCIe
  node (the pre-existing sp804/uart schema messages remain, same as the other
  lanes' baseline);
- `fdtget` read-back on the integrated DTB: `pcie@10160000` /
  `pcie@10164000` compatible `hisilicon,luofu-pcie`, `reg-names =
  "dbi" "misc" "config"`, `reset-names = "apb" "pcs" "phy" "ctrl"`,
  `status = "disabled"`, the two `wifi@0,0` children
  `compatible = "pci59e7,0005"`, `status = "disabled"`.

### 5.5 Config derivation

The new symbol is a `bool` selected by nothing, but `select PCIE_DW_HOST`
brings in six hidden/select-driven symbols.  The delivered annotated delta was
canonically re-derived in the clone:

- `yes "" | make kernel_oldconfig` twice with the patch applied: **RC=0**
  both times, the two emitted deltas are byte-identical
  (sha256 `6ca2221564d6266ce9005a12210b7db118749de013333876e895072fb38deed6`),
  and identical to the first re-derivation run;
- the emitted symbol set is **identical** to the delivered annotated
  `config-6.18` (306 symbols, `CONFIG_SYMBOLS_IDENTICAL`), including
  `CONFIG_PCIE_LUOFU_HOST=y`;
- the clone's target config was restored to the delivered annotated file after
  the derivation.

### 5.6 Style and discipline

- `checkpatch.pl --no-tree -f`: **0 errors / 0 warnings / 0 checks** on the
  driver (467 lines) and on the YAML binding (126 lines);
- `shared.patch` applies cleanly to the pre-apply repository state (dry-run
  RC=0, apply RC=0) and the applied files are byte-identical to the delivered
  post-apply files;
- static scans (`60-static-checks.log`): no raw `readb/readw/writeb/writew`
  in the driver; the only `readl()` calls are the aligned-dword accessor's
  `readl(base + (reg & ~3))`/the RMW read; no read of the misc window; no
  `0x400392f0`/`0x4016010c`/forbidden-window raw use; no
  lab/bench/local-path breadcrumbs in the driver, binding or patch; no
  vendor-only property names (`iatu_rc`/`iatu_ep`/`acp-gate`/`pcie-gpios`/
  `gpio-delay`) in the YAML.

## 6. Integration notes for the lead (shared docs; not edited here)

1. **Patch 013** is already in `patches-6.18/`; **shared.patch** was applied by
   you during the lane (repository DTS/config hashes match the post-apply
   hashes above).  No shared repository file was edited by this lane.
2. **Patch 014 interaction:** 013 consumes the reset API only; it does not care
   about the provider internals.  With 014 the deassert in probe releases the
   active-low lines as intended.  The driver header documents the polarity
   history and points at 014; if the final series keeps 014, no change is
   needed in 013.
3. **Enabling the nodes:** at bench, flip `status = "okay"` on `pcie0`/
   `pcie1` only.  The W-Fi endpoint children stay disabled (their driver is
   [X]); enumeration comes from config space, so they do not need nodes.
4. **Expected first-boot observations** (from the measured live vendor state):
   RC0 Link Status 0x7012 (DL_ACTIVE, 5 GT/s x1), iATU viewport detect with 3
   outbound windows, endpoint `59e7:0005` at bus 0 device 0 on both domains.
   The driver logs the LTSSM/DEBUG1/link state after enumeration.
5. If the repository README/SKELETON-SMOKE need a line for this lane, the
   "clean compile, no boot" status applies exactly as for the other driver
   lanes; the add-on is that two root-complex nodes are now present and
   disabled with a real driver behind them.
6. Local evidence copy: `build/linux-kbuild/waves/2026-11-18-pcie/`
   (gitignored); the incoming directory includes `shared.patch` only, the
   patch file lives in `patches-6.18/`.

## 7. Suggested SKELETON-SMOKE session section (for the lead)

- patch 013 adds `drivers/pci/controller/dwc/pcie-luofu.c` (DWC host glue) with
  the `hisilicon,luofu-pcie` YAML binding; `CONFIG_PCIE_LUOFU_HOST=y`;
- host-only validation: pristine dry-run 001-013 RC=0, prepare RC=0, full
  compile RC=0 with **0 warnings**, zImage sha256 `392d5861...`, TEXT_OFFSET
  0x00608000 unchanged, dt_binding_check RC=0, board DTS zero warnings,
  config derivation idempotent (306 symbols identical), checkpatch 0/0/0;
- the two root-complex DTS nodes remain `disabled`; the write path has never
  been measured and the endpoints are [X]; nothing boots and no enumeration is
  claimed.

## 8. What this proves and what it does not

**Proves:** the port is a real DWC host controller that builds cleanly as
built-in code with the openwrt main / linux-6.18.60 / GCC 14.4.0 toolchain and
the full platform series; its binding validates and its DTS nodes compile with
zero warnings; every access class it uses is aligned-dword or full-word-store
by construction and by static scan; the config delta is canonically derived and
idempotent; the CRG dependencies (clock, four resets) are wired through the
standard providers.

**Does not prove:** that the wrapper stores are accepted or that the RC mode /
LTSSM sequence is right; that the configuration window actually answers the
accesses with the ATU programming this driver performs; that the link comes up
under this driver; that the PCI core enumerates the endpoints (or that their
resources assign); that any interrupt works (none is wired); anything about the
Wi-Fi endpoint (driver [X]); any thermal/electrical/power sequencing (endpoint
GPIOs not modelled); or anything about Wi-Fi operation.  No boot, no image, no
install path, no benchmark.  Publication remains NOT READY.

## 9. Remaining TODOs / open questions

1. **Validate the write path (highest priority, attended hardware run):** the
   wrapper RC-mode store, the iATU/LTSSM enable stores and all DBI writes are
   unmeasured on this board.  The first run should be an attended boot with the
   node enabled and the serial console captured; the driver only logs, so a
   failure is diagnosable.
2. **iATU-enable bit semantics ([U]):** does the wrapper's iATU-enable bit gate
   iATU register writes, outbound translations, or both - and is keeping it
   asserted together with the LTSSM bit correct?  If per-access CFG0
   re-programming turns out not to take, program the CFG0 viewport once and
   serve bus 0/device 0 from a static window (vendor style) or re-work
   `start_link`.
3. **Endpoint power/reset GPIO sequencing ([U]):** the vendor's `pcie-gpios`
   (board overlay; gpio 0x14/0x15/0x17 or 0x1c depending on board_id) are not
   modelled.  If the endpoint is unpowered at boot, enumeration cannot work
   until a documented binding (e.g. `reset-gpios`/regulator) is designed and
   the polarity/delays are measured.
4. **DWC core version ([U]):** `dw_pcie_version_detect()` reads DBI+0x8f8/
   +0x8fc.  The measured iATU layout is the legacy (pre-4.80a) one; if the
   version register reads a value >= 4.60a the framework will also write the
   0x920 upper-limit register.  Read the version word at bench before trusting
   it; if it is bogus, set `pci->version` explicitly in the glue.
5. **radm/linkdown interrupts ([U]):** their status/clear registers are the
   write-only misc window's +0x28/+0x2c words; no safe handler can be written
   until the clear semantics are established.  v1 does not request them.
6. **MSI/INTx routing ([U]):** neither the DWC MSI-controller line nor the
   endpoint interrupt route is described in the vendor devicetree; v1
   enumerates without interrupts.  Endpoint functionality is [X] anyway.
7. **dma-ranges / inbound iATU windows ([U]):** without `dma-ranges` the
   framework programs no inbound window (and disables pre-existing ones).  The
   endpoint-firmware inbound map belongs to the Wi-Fi port; it must be designed
   with that driver, not guessed here.
8. **ACP coherency / cache attributes ([U]):** the vendor `hi_acp.ko` programs
   attributes through 0x10100174; whether a Linux port needs it is a
   hardware/bench question.  Do not read 0x10161000 to find out.
9. **ASPM/L1SS ([U]):** the vendor has an ASPM enable path and L1SS pinmux
   writes; v1 keeps ASPM/L1SS off - the measured link is 5 GT/s x1 with
   L0s/L1 support advertised, and no ASPM register is written by this driver.
10. **Second function/deeper bus configuration ([U]):** see decision 3; the
    measured topology has one function per root complex.
11. **Reset inter-step delay ([U]):** the vendor's delay values are not
    recorded; v1 uses a conservative short sleep between deasserts.

## 10. Blockers

- **CRG reset polarity (resolved):** patch 004's provider was inverted relative
  to the measured active-low reset word; the lead's **patch 014** fixes it
  (assert clears, deassert sets, status inverted).  With 014 applied, probe's
  deassert releases the four lines.  The PCIe DTS nodes stay disabled until
  hardware validation regardless.
- **Device write path unmeasured (open):** the wrapper stores and DBI writes
  are the only reason the nodes are disabled; there is no host-side way to
  close this.  It needs the attended bench session.

## 11. Regeneration commands (host-only)

~~~sh
# isolated clone (never the shared one)
cp -a --reflink=auto /home/shibbityshwab/openwrt-lab /home/shibbityshwab/openwrt-lab-pcie
cp -a <repo>/target-skeleton/target/linux/hisilicon /home/shibbityshwab/openwrt-lab-pcie/target/linux/
rm -rf /home/shibbityshwab/openwrt-lab-pcie/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu
cd /home/shibbityshwab/openwrt-lab-pcie
make target/linux/prepare V=s
make target/linux/compile V=s -j16

# binding + board DTS (patched kernel tree provides dtc and dt-bindings)
K=/home/shibbityshwab/openwrt-lab-pcie/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
PATH=<wave>/dtschema-venv/bin:$PATH make -C $K ARCH=arm dt_binding_check \
    DT_SCHEMA_FILES=Documentation/devicetree/bindings/pci/hisilicon,luofu-pcie.yaml
cpp -nostdinc -I $K/include -I $K/arch/arm/boot/dts -undef -D__DTS__ \
    -x assembler-with-cpp -o /tmp/luofu.dts <dts/luofu-r116.dts>
$K/scripts/dtc/dtc -I dts -O dtb -o /tmp/luofu.dtb /tmp/luofu.dts

# config derivation
yes "" | make kernel_oldconfig    # run twice, compare the emitted target config

# local evidence
build/linux-kbuild/waves/2026-11-18-pcie/   (gitignored)
~~~
