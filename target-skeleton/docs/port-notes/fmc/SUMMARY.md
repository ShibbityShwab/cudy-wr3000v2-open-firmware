> Integration note: the integration patch described below was applied to the repository
> on 2026-11-18 and is not kept in the tree; this SUMMARY is the durable record.

# FMC SPI-NAND controller port - incoming summary (2026-11-18)

**Status: a first upstream-style driver for the board's FMC SPI-NAND controller is written,
with a YAML binding and the board DTS converted; it applies to pristine `linux-6.18.60`,
builds as a built-in into the board kernel with zero warnings, the board DTS compiles with
zero warnings, the binding passes `dt_binding_check`, and the kernel-config delta is
canonically derived. NOTHING HAS BEEN BOOTED OR RUN ON HARDWARE. The FMC ECC/DMA-OOB/
quad-enable semantics and the partition behaviour are unverified; publication remains NOT
READY.**

Host-only work in an isolated reflink copy of the clone
(`openwrt-lab-fmc`; `openwrt main` `27663a0`, `linux-6.18.60`, the tree's own OpenWrt GCC
14.4.0 toolchain). The original `openwrt-lab` clone was only read (a parallel port was
using it); no router, UART, TFTP, flash or bench tool was touched.

---

## 1. Framework decision (read this first - it differs from the task's first choice)

The task text asked for the **raw-NAND framework** (`drivers/mtd/nand/raw`). That framework
cannot bind this hardware, and the driver therefore uses the **mainline SPI-NAND stack
through a SPI-MEM controller** instead:

- the die is an **SPI-NAND** part (ESMT F50L1G41LC): page load `0x13`, read-from-cache
  `0x6b`, program load `0x32`, program execute `0x10`, block erase `0xd8`, READ ID
  `0x9f` (the opcodes are in `include/linux/mtd/spinand.h` for the mainline driver);
- the raw-NAND core (``drivers/mtd/nand/raw/nand_base.c``) drives parallel-NAND sequences
  (`NAND_CMD_READ0` `0x00`, `NAND_CMD_READID` `0x90`, `NAND_CMD_SEQIN` `0x80`,
  `NAND_CMD_ERASE1/2`, ...) and a `0x90` READ ID, which this die does not implement;
- the controller is in SPI mode: `FMC_CFG` bit 2 = 0 (the lab driver refuses the block when
  that bit is 1 = raw/parallel mode), and the board is wired for SPI-NAND;
- the mainline SPI-NAND stack provides exactly what the design asks for and TARGET-DESIGN
  section 6.2/7.1 lists as needed: chip detection, the on-die ECC engine, the ESMT OOB
  layout, real bad-block handling and partition parsing. Re-implementing those in a custom
  `mtd_info` (the lab driver's shape) is the thing an upstream submission must not do.

The repo's own port spec recommends this too: `opensource/docs/phase50/nand-fmc-port-spec.md`
section (f) ("Recommended architecture: reuse the mainline SPI-NAND stack via a thin
spi-mem controller driver"). Upstream precedent for the shape: `drivers/spi/spi-qpic-snand.c`,
`drivers/spi/spi-amlogic-spifc-a4.c`.

Driver location: `drivers/spi/spi-luofu-fmc.c` (a SPI controller, so `drivers/spi/` is the
right home; a NAND-controller binding under `Documentation/devicetree/bindings/mtd/` is
still the right binding home, matching the existing `hisilicon,fmc-spi-nor.txt` and
`hisi504-nand.txt` entries).

**OpenWrt dependency - the chip entry:** F50L1G41LC support is **not in vanilla
`linux-6.18.60`**; it is a v6.19 addition (upstream commit `b98994cb9bc2`) that this 6.18
OpenWrt tree receives through the generic backport
`target/linux/generic/backport-6.18/434-v6.19-mtd-spinand-esmt-add-support-for-F50L1G41LC.patch`.
The driver therefore probes only because that backport is applied in this tree; an upstream
submission needs the chip entry (or a target kernel bump). This is called out in
TARGET-DESIGN and README by the shared patch.

---

## 2. Deliverables

| file | sha256 |
| --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/006-arm-mtd-nand-luofu-fmc.patch` | `1f8809e98be649fb915c33d0ceaadb0c7e98509068347e72557839d3c7f5865e` |
| `target-skeleton/docs/port-notes/fmc/shared.patch` (this delivery) | see section 6 |
| driver `drivers/spi/spi-luofu-fmc.c` (inside patch 006) | `6231331291a8a2746359f2d7ddbe115e9ea73d4a201008a321ff49796f373e9c` (635 lines, checkpatch 0/0) |
| binding `Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml` | `8d9d6f045d79a7f4ed1f98270905d7e65b82d82b3b2c71f640284091718b019d` (106 lines, checkpatch clean) |
| `target-skeleton/target/linux/hisilicon/luofu/config-6.18` (new version, shared.patch) | `2b5493d2797d12b5b77eb13027daee7e07b331d63b590ba66c305231fd67c58a` |
| `target-skeleton/dts/luofu-r116.dts` (new version, shared.patch) | `d1cca5f9b730f5b01dcd2cab010680013ac6b46b02713f8650dd8f2ccfc4ed0d` |

Patch 006 touches exactly four files:

- `drivers/spi/Kconfig`: `config LUOFU_FMC` (tristate, `depends on ARCH_LUOFU ||
  COMPILE_TEST`, `depends on MTD_SPI_NAND`);
- `drivers/spi/Makefile`: `obj-$(CONFIG_LUOFU_FMC) += spi-luofu-fmc.o`;
- `drivers/spi/spi-luofu-fmc.c` (new, 635 lines, checkpatch 0/0): the SPI-MEM controller;
- `Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml` (new, 106 lines).

Shared-file edits (in `shared.patch`, applied by the lead):

- `target-skeleton/target/linux/hisilicon/luofu/config-6.18`: `CONFIG_LUOFU_FMC=y` plus the
  SPI/SPI-NAND stack (`CONFIG_SPI`, `CONFIG_SPI_MASTER`, `CONFIG_SPI_MEM`,
  `CONFIG_MTD_SPI_NAND`, `CONFIG_MTD_NAND_CORE`, `CONFIG_MTD_NAND_ECC`), with [N]
  provenance markers consistent with the existing entries;
- `target-skeleton/dts/luofu-r116.dts`: the FMC node becomes `spi@10a20000` (SPI bus) with
  `reg-names = "regs", "window"`, the CRG cells `clocks = <&crg LUOFU_CLK_SFC>` and
  `resets = <&crg 0x2c 0x0>`, and one `flash@1` (`compatible = "spi-nand"`, `reg = <1>`,
  quad bus widths) carrying the same 17 fixed partitions; no vendor props left;
- `target-skeleton/README.md`: patch-006 row, status paragraph, driver bullet, checked-list
  bullet and the `config-6.18`/DTS rows extended;
- `docs/TARGET-DESIGN.md`: the section 6.2 and 7.1 FMC rows updated to "**written** as
  patch `006`" with the open items;
- `docs/SKELETON-SMOKE.md`: new "2026-11-18 (sixth session): FMC SPI-NAND controller"
  section with the exact commands/results.

---

## 3. Exact commands and results (all host-only, local evidence in
`build/linux-kbuild/waves/upstream-6.18-fmc-20261118/`)

### 3.1 Patch applies to pristine linux-6.18.60 (PASS)

Tree `a` = pristine `linux-6.18.60` unpacked from
`/home/shibbityshwab/openwrt-lab/dl/linux-6.18.60.tar.xz`; tree `b` = a + the patch files.

```
$ cd .../upstream-6.18-fmc-20261118/a
$ patch -p1 --dry-run -i 006-arm-mtd-nand-luofu-fmc.patch
checking file drivers/spi/Kconfig
checking file drivers/spi/Makefile
checking file drivers/spi/spi-luofu-fmc.c
checking file Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml
RC=0
```

### 3.2 OpenWrt prepare (PASS)

Target `001`-`006` + the shared-file config/DTS edits copied into the isolated clone copy:

```
$ cd /home/shibbityshwab/openwrt-lab-fmc && make target/linux/prepare
RC=0
```

Prepare log shows 004/005/006 applied; 0 `.rej`/`.orig` files; the patched tree contains
`drivers/spi/spi-luofu-fmc.c` and the YAML.

### 3.3 Full kernel compile (PASS)

```
$ make target/linux/compile V=s -j16
  CC      drivers/spi/spi-luofu-fmc.o
...
  Kernel: arch/arm/boot/zImage is ready
RC=0    (START 2026-11-18T03:27:22Z, END 03:29:11Z = 109 s wall clock)
```

- `arch/arm/boot/zImage`: **3,248,984 B**, sha256
  `51f340adc9d4e83c6b4c55be93d43453d65947126c9c805309a5d08e99d859ba`.
- kernel `.config`: `CONFIG_LUOFU_FMC=y`, `CONFIG_MTD_SPI_NAND=y`,
  `CONFIG_MTD_NAND_CORE=y`, `CONFIG_MTD_NAND_ECC=y`, `CONFIG_SPI=y`,
  `CONFIG_SPI_MASTER=y`, `CONFIG_SPI_MEM=y` (`CONFIG_LUOFU_CRG=y`/`CONFIG_LUOFU_WDT=y`
  from 004/005 stay on).
- the driver is built-in and linked: `System.map` has `luofu_fmc_probe` (`c0b9bcf4`),
  `luofu_fmc_exec_op`, `luofu_fmc_dma_xfer`, `luofu_fmc_of_match`,
  `luofu_fmc_mem_ops` and the level-6 `device_initcall`.
- **0 compiler warnings/errors from the new files**: the only `spi-luofu-fmc` line in the
  build log is the `CC` line. The other warning strings are pre-existing
  `modpost: missing MODULE_DESCRIPTION()` lines for unrelated generic modules (netfilter,
  phylink, ppp, ...) plus one kernel kconfig-sync notice printed before the build on the
  freshly re-extracted tree (not a source warning).
- load contract unchanged: `-DTEXT_OFFSET=0x00608000` in both
  `arch/arm/kernel/.head.o.cmd` and `arch/arm/boot/compressed/.head.o.cmd`, and
  `System.map` has `_text = stext = 0xc0608000` (load/entry `0x80608000` via the loader
  staging base, exactly as in the earlier sessions).
- `CONFIG_WERROR=y` is set in the kernel `.config`.

### 3.4 Board DTS -> DTB with the patched kernel's own dtc (PASS, zero warnings)

```
$ cpp -nostdinc -I <kernel>/include -I <kernel>/arch/arm/boot/dts -undef \
      -D__DTS__ -x assembler-with-cpp -o luofu-r116.dts.preprocessed dts/luofu-r116.dts
$ <kernel>/scripts/dtc/dtc -I dts -O dtb -o luofu-r116.dtb luofu-r116.dts.preprocessed
DTC 1.7.2-g52f07dcc, RC=0, stderr EMPTY
```

- DTB: **9,163 B**, sha256
  `3541b267ca666e90d921d48ccea2d3394d657e68805aa5fff1b1e4009de154df`.
- the `spi@10a20000` node has compatible `hisilicon,luofu-fmc`, `reg = <0x10a20000 0x1000>,
  <0x1c000000 0x100000>` with `reg-names = "regs", "window"`, `clocks = <&crg 0>`
  (`LUOFU_CLK_SFC`, the CRG phandle), `resets = <&crg 0x2c 0>`, `#address-cells = <1>`,
  `#size-cells = <0>`; the child `flash@1` has `compatible = "spi-nand"`, `reg = <1>`,
  `spi-tx/rx-bus-width = <4>` and 17 fixed partitions (verified with `fdtget`).

### 3.5 Binding validation (PASS)

```
$ PATH=<dtschema-venv>/bin:$PATH make ARCH=arm dt_binding_check \
      DT_SCHEMA_FILES=mtd/hisilicon,luofu-fmc.yaml
  SCHEMA  Documentation/devicetree/bindings/processed-schema.json
  CHKDT   ./Documentation/devicetree/bindings
  DTEX    Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.example.dts
  DTC [C] Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.example.dtb
RC=0
```

dtschema **2026.9** in a throwaway venv. `yamllint` is not installed, so the tree's YAML
lint step is skipped with its own warning - that is the only step not exercised. The board
DTB's FMC subtree (plus the CRG provider it references) also validates against the same
processed schema: `dt-validate` RC=0, empty stderr.
Note: TARGET-DESIGN names `mtd/nand/hisilicon,luofu-fmc.yaml`, but 6.18 has no
`bindings/mtd/nand/` directory; the binding is placed flat under `bindings/mtd/`
(consistent with `hisilicon,fmc-spi-nor.txt` / `hisi504-nand.txt`).

### 3.6 Kernel-config provenance (PASS)

Canonical derivation in the clone copy (the OpenWrt path: merge generic + target delta ->
kernel `oldconfig` -> diff back to the target delta):

```
$ yes '' | make -C target/linux TOPDIR=$PWD oldconfig V=s
make_rc=0     (the `yes` feeder exits on SIGPIPE after make finishes; that is expected)
```

- target emitted **285** config-symbol lines, identical as a set to the annotated
  `luofu/config-6.18` (`CONFIG_SYMBOLS_IDENTICAL`; only the [N] annotation lines move
  symbols out of the emitted sort order);
- two consecutive canonical runs are **byte-identical**
  (`CONFIG_DERIVATION_IDEMPOTENT`);
- the annotated delta and the canonical delta merge with `generic/config-6.18` to the
  **identical kernel config** (`ANNOTATED_CONFIG_MERGES_IDENTICALLY`).

### 3.7 Static sanity (PASS)

- `scripts/checkpatch.pl --no-tree -f`: **0 errors, 0 warnings** for
  `drivers/spi/spi-luofu-fmc.c` (635 lines) and
  `Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml` (106 lines).
- no lab/bench breadcrumbs in the delivered driver/binding: 0 matches for `lab`, `bench`,
  `opensource`, `target-skeleton`, sysctrl, `0xC0DE` or breadcrumb references (the driver
  comments cite only the register model and the vendor kernel).
- the driver consumes the CRG cells instead of hard-coding them:
  `clocks = <&crg LUOFU_CLK_SFC>`, `resets = <&crg 0x2c 0>` (`sfc_rst`); it obtains them
  with `devm_clk_get_enabled(dev, "sfc_clk")` and
  `devm_reset_control_get_exclusive(dev, "sfc_rst")` and deasserts the reset (the
  bootloader has already released the block).

---

## 4. What the driver does (brief)

- `platform_driver` for `compatible = "hisilicon,luofu-fmc"`; maps the register file and
  the 1 MiB data window (`reg` entries 0/1), enables the `sfc_clk` gate, deasserts
  `sfc_rst`, programs the two documented mode bits (normal mode, write-protect off),
  allocates one 0x2200-byte coherent DMA buffer and arms `SADDR_D0/D1/OOB`.
- registers a `spi_controller` with `mem_ops` {.supports_op, .exec_op}; the SPI-NAND core
  creates the `flash@1` (`spi-nand`) child from DT and drives it.
- `exec_op` dispatch:
  - page load `0x13` / program execute `0x10`: command engine / DMA write respectively
    (page load also records the row the following cache read needs);
  - page-cache reads (`0x6b` etc., data-in > 8 bytes): one full-page DMA transfer, served
    from the logical image {data, OOB}; page-cache writes (`0x32` etc.): staged, then the
    DMA write runs at program execute using the row from that op;
  - everything else (reset, WREN, get/set feature, READ ID, block erase): command engine,
    `CMD/ADDRL/OP_CFG/DATA_NUM/OP` with the OP phase encoding that reproduces every
    observed vendor command byte (0x81/0x85/0xc1/0xc5/0x185);
  - all command-engine ops clear/restore the FMC ECC type field, as the vendor/BSP do.
- The two page-data `OP_CFG` interface packs (0x1D1 read / 0x1D0 write, chip-select bits
  added) and the ECC type values (0 command / 1 page) are the measured register values from
  the lab driver's proven read path and byte-exact write path, not a field decomposition
  the driver claims to understand.
- Page/OOB geometry (2048/64) is driver data attached to the compatible (the DMA engine
  transfers one page and the controller cannot discover the die geometry); every op is
  validated against it.
- No IRQ handler: every known implementation polls and no devicetree wires an IRQ for this
  block. Completion polls: OP_CTRL bit 0 for the read engine, INT_STATUS bit 0 for the
  write engine - both as measured.
- Deliberately not written: the fifteen undecoded vendor init words (their meaning is
  unknown; only the two documented mode bits are set). No die reset in the controller
  probe (the SPI-NAND core issues its own).

---

## 5. What is proven vs unverified

Proven (host-only): patch applies to pristine 6.18.60 and in the OpenWrt clone; full
kernel compile as a built-in with 0 warnings from the new files and `CONFIG_WERROR=y`;
board DTS compiles with the kernel's own dtc with 0 warnings; binding passes
`dt_binding_check` and the board DTB's FMC subtree validates; config delta is canonically
derived (idempotent, merges identically); checkpatch clean; no lab breadcrumbs; load
contract unchanged (`-DTEXT_OFFSET=0x00608000`, `_text = stext = 0xc0608000`).

**Not verified - hardware behaviour (all open, no boot/no bench/no device):**

1. **FMC ECC semantics.** Whether `FMC_CFG` ECC type 1 is a host ECC engine or an on-die
   pass-through mode is not established. The driver uses the mainline on-die ECC engine
   (the ESMT die reports 1 bit/512 B) and leaves the measured type values; if the FMC is
   actually doing host ECC, a `nand-ecc-engine` host engine implementation is the follow-up.
2. **OOB bytes from the DMA engine.** The lab proof only ever consumed page data; the OOB
   region at `SADDR_OOB` was armed but its contents were never validated against the
   vendor's `/dev/mtd` view. Bad-block markers and ECC status reporting depend on it.
3. **Quad-enable after reset.** The board's bootloader runs the die in quad mode, and the
   mainline ESMT entry has no `SPINAND_HAS_QE_BIT` quirk; whether the die reset issued by
   the SPI-NAND core at probe clears the QE bit (and therefore whether the first quad read
   works under Linux) is unknown. A bench bring-up must check it; the workaround (if
   needed) is a chip quirk, not a DT property.
4. **Page-load command 0x13 through the command engine.** The measured-working page read
   let the DMA engine perform the whole page operation and never issued a separate page
   load; the driver does issue one (as the SPI-NAND core requires) and then the DMA read.
5. **The 15 undecoded vendor init words** (timing/pwidth/opidle are bootloader-programmed;
   the rest are unidentified) are not written. Whether the die needs any of them is unknown.
6. **Program execute ordering.** The measured write path never sends a separate
   PROGRAM EXECUTE; the driver performs the DMA write at program-execute time (the only
   point where the row is known). Whether the controller's write engine accepts that exact
   sequence with the SPI-NAND core's extra WREN is unverified.
7. **Partition semantics**: read-only flags and the `u-boot,env` representation are still
   the earlier draft's open items; the driver exposes whatever the `fixed-partitions` child
   describes.
8. **The ESMT F50L1G41LC chip entry is not in vanilla 6.18.60** (see section 1); it comes
   from OpenWrt's generic backport 434 in this tree.

Also not done by design: no IRQ, no restart handler, no die reset, no vendor init block,
no boot/bench claims.

---

## 6. How to integrate (for the lead)

1. Copy the patch:
   `cp 006-arm-mtd-nand-luofu-fmc.patch <repo>/opensource/target-skeleton/target/linux/hisilicon/patches-6.18/`
2. Apply the shared-file patch inside the `opensource` submodule (paths are relative to
   `opensource/`):
   ```
   $ cd <repo>/opensource
   $ git apply --check target-skeleton/docs/port-notes/fmc/shared.patch
   $ git apply target-skeleton/docs/port-notes/fmc/shared.patch
   ```
   `patch -p1` from the same directory works too (verified). The patch touches
   `target-skeleton/{README.md,dts/luofu-r116.dts,target/linux/hisilicon/luofu/config-6.18}`
   and `docs/{SKELETON-SMOKE.md,TARGET-DESIGN.md,TARGET-SKELETON.md}`. It is **regenerated
   against the net-lane-merged working tree** (patches `007`-`009` and
   `docs/port-notes/net/` merged in): `git apply --check
   target-skeleton/docs/port-notes/fmc/shared.patch` from `opensource/` returns **RC=0**, and
   the resulting six files byte-match the rebased versions the FMC port validated. The
   DTS/config hunks were 3-way merged so the net-lane text is preserved; the README /
   SKELETON-SMOKE / TARGET-DESIGN / TARGET-SKELETON edits were rebased onto the network
   session's text instead of replacing it.
3. The network lane (`007`-`009` plus `docs/port-notes/net/shared.patch`) is already
   merged into the working tree; this patch was regenerated on top of it. Apply this one
   after the net lane if the order is ever replayed. The two port series do not otherwise
   depend on each other.
4. If the config delta ever needs re-deriving, run the OpenWrt path
   `yes '' | make -C target/linux TOPDIR=$PWD oldconfig` - it emits the same 285 symbols;
   do not renumber or reorder the `[N]` blocks by hand.

Validation copy: `/home/shibbityshwab/openwrt-lab-fmc` (isolated reflink copy of the
OpenWrt clone; disposable). Evidence (local-only, gitignored):
`build/linux-kbuild/waves/upstream-6.18-fmc-20261118/` - including
`evidence-final.txt`, `logs/`, `dtscheck-final/luofu-r116.dtb`,
`dtscheck/fmc-crg-subtree.dtb`, `config-derivation/` and the pristine/patched kernel trees
`a`/`b` used to generate patch 006.

## 7. Remaining TODOs / blockers (honest)

1. Attended bench bring-up of one generated kernel: confirm the command-engine encodings
   and address byte order, the DMA completion protocol, the quad-enable state after the
   SPI-NAND core's reset, the OOB path and the ECC status reporting; then decide whether any
   of the fifteen vendor init words is required.
2. Upstream: land the ESMT F50L1G41LC chip entry (v6.19) or move the target to a kernel
   that has it; add `MAINTAINERS` entries; review the driver-data page/OOB geometry
   approach; decide whether the reset should be pulsed.
3. The image recipe/rootfs/sysupgrade wiring and one attended boot remain out of scope
   (TARGET-DESIGN sections 5 and 9).

## 8. shared.patch hash

`dda5f159253e322e8dcd968b4533113f890a6cb72db64973a30983d1288d7455`
(552 lines, 6 files, 358 insertions / 69 deletions; `git apply --check` **RC=0 against the
current repo working tree** - the tree with the net lane already merged; `patch -p1
--dry-run` RC=0; all six resulting files byte-identical to the rebased versions).
