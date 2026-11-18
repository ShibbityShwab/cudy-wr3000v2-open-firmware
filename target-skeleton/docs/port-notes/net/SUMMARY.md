> Integration note: the integration patch described below was applied to the repository
> on 2026-11-18 and is not kept in the tree; this SUMMARY is the durable record.

# incoming/net - network trio port summary (MDIO bus + Genius PHY + GMAC)

**Task:** task-16 (parallel port lanes, agent `driver-net`). **Date:** 2026-11-18.
**Scope:** port the lab network drivers `tools/lab-src/drivers/net/ethernet/luofu/
{luofu-mdio.c,luofu-phy.c,luofu-gmac.c}` to kernel 6.18 in the OpenWrt target skeleton,
with own compatibles, YAML bindings, Kconfig/Makefile hooks and DTS/config updates, and
validate host-side in the `openwrt main` clone. **No device was touched** (no router, no
UART, no TFTP, no flash, no bench tool). Host-only, evidence-graded.

This directory is an **incoming** deliverable: it carries no shared-file edits directly.
Apply `shared.patch` (below) and add the three patch files that are already in
`patches-6.18/`.

---

## 1. Deliverables

### 1.1 New kernel patches (already in the repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/007-net-mdio-luofu.patch` | `dcbb2821b99f2f5b265596999b0d88fc8605561c4b2453d8857ca54d0d512ccb` |
| `target-skeleton/target/linux/hisilicon/patches-6.18/008-net-phy-luofu.patch` | `676e332e5072608b7193a57213605d5af5cda26e11586d7a9c26fe6b14365912` |
| `target-skeleton/target/linux/hisilicon/patches-6.18/009-net-gmac-luofu.patch` | `96022d6a61c7ef878397dbe2a28b56c00fbf68b6b3ea1bbc3df7ae5e0a2aecee` |

Numbering: `006` belongs to the parallel FMC/NAND lane; this lane is `007`-`009`.
Each patch is a bare unified diff (`--- a/...` / `+++ b/...`, no `diff --git`) in the
style of the accepted `004`/`005`, and applies with `patch -p1` in the OpenWrt
platform-patch order.

**`007` - MDIO bus controller (complete port)**
- `drivers/net/mdio/mdio-luofu.c` (new): platform driver on the 6.18 MDIO bus API
  (`devm_mdiobus_alloc_size` + `of_mdiobus_register`), `devm_clk_get_enabled()` gate
  clock, the seven CRG reset lines as a `devm_reset_control_array_get_exclusive()` array
  deasserted after the clock is on, and the polled MMIO frame handshake
  (`0x20` done/ack, `0x2c` busy, `0x30` frame, `0x38` data) with
  `readl_poll_timeout_atomic()` (10 us, 100 ms bound).
- `drivers/net/mdio/{Kconfig,Makefile}`: `CONFIG_LUOFU_MDIO`
  (`tristate`, `depends on HAS_IOMEM && OF_MDIO`, inside the existing `if PHYLIB` block).
- `Documentation/devicetree/bindings/net/hisilicon,luofu-mdio.yaml` (new).

**`008` - Genius PHY driver (complete port)**
- `drivers/net/phy/luofu-phy.c` (new): PHYLIB driver matched by PHY ID
  `0x20669a00` / mask `0xffffff00`; generic clause 22 ops
  (`genphy_config_aneg`/`genphy_read_status`/`genphy_suspend`/`genphy_resume`).
- `drivers/net/phy/{Kconfig,Makefile}`: `CONFIG_LUOFU_PHY`.
- **No YAML, deliberately:** PHY drivers are matched by PHY ID, not by a DT compatible;
  the PHY children stay standard `ethernet-phy-ieee802.3-c22` nodes which the MDIO
  binding validates. The vendor per-PHY firmware patch is a documented TODO (not loaded).

**`009` - GEMAC (PARTIAL: binding + resource validation only, no data path)**
- `drivers/net/ethernet/hisilicon/luofu-gmac.c` (new): claims the documented binding,
  validates `reg`/`clocks`/`resets`/`phy-mode`/`phy-handle`, then **refuses to probe**
  with `-EOPNOTSUPP`. It registers no `net_device`, has no `ndo_start_xmit`, no NAPI and
  no ring/DMA code. Rationale is in the file header: the TX/RX ring and DMA programming
  model has never been decoded (the lab driver had no ring code; its xmit dropped frames;
  the vendor's transmit helper lives in another module), a wrong access to the block can
  hang the AHB bus, and a netdev that cannot carry a packet would be a stub. The DTS GEMAC
  nodes are `status = "disabled"`, so a normal boot never reaches probe.
- `drivers/net/ethernet/hisilicon/{Kconfig,Makefile}`: `CONFIG_LUOFU_GMAC`
  (`tristate`, `depends on OF`).
- `Documentation/devicetree/bindings/net/hisilicon,luofu-gmac.yaml` (new): describes the
  five instances (ports 8..12 of the internal switch; 0 RGMI unused, 1..4 GMII; the wired
  jack is instance 3 / 0x14480000 / port 11), one clock, two CRG reset lines and
  `phy-mode`/`phy-handle`. No vendor-only properties (`port`, `cnt-clr` are not carried).

### 1.2 `shared.patch` (apply to the current repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/docs/port-notes/net/shared.patch` | `ca9bf98709d666b078d8f03175ae231cdbabc2f746eeb235fe3eb730fc4f8580` |

- 754 lines, six files, **git-style** (`diff --git a/... b/...`).
- Verified: `git apply --check` RC=0 and `patch -p1 --dry-run` RC=0 against the current
  repo files; applying it in a throwaway copy reproduces all six intended files
  byte-for-byte (comparison PASS).
- Files and what they carry:
  - `target-skeleton/target/linux/hisilicon/luofu/config-6.18`: adds
    `CONFIG_LUOFU_GMAC=y`, `CONFIG_LUOFU_MDIO=y`, `CONFIG_LUOFU_PHY=y` as `[N]`
    entries with provenance comments; updates the `[N]` legend (network trio reuses the
    already-present `OF_MDIO`/`MDIO_BUS`/`PHYLIB` symbols, no `PHYLINK` change) and
    removes the three now-real symbols from the "not a symbol in 6.18" list.
  - `target-skeleton/dts/luofu-r116.dts`: MDIO node gains the seven CRG `resets` +
    clean `reset-names` (`mdio`, `gephyall`, `gephy0`..`gephy4`); all five GEMAC
    nodes gain their two CRG reset lines and `reset-names = "mac-logic", "mac"`; GEMAC
    nodes are changed from `okay` to `disabled` (no functional driver) with a comment
    explaining why; binding comments point at the two new YAMLs.
  - `target-skeleton/README.md`, `docs/SKELETON-SMOKE.md` (new "sixth session" section),
    `docs/TARGET-DESIGN.md` (`6.2 rows and `7.1 rows for GEMAC/MDIO/PHY, status line),
    `docs/TARGET-SKELETON.md`: status rows updated to say the trio exists as host-compiled
    drafts, and that MDIO+PHY are complete ports while GEMAC has no data path.
- Post-apply hashes (for a sanity check by the lead):
  - `target-skeleton/dts/luofu-r116.dts` = `989da320e753d9f7cd4f0d51563d55b3c784d713d3ef0d73bc71c85a1ef2fda6`
  - `target-skeleton/target/linux/hisilicon/luofu/config-6.18` = `6910879e7ef9ebfdb6e4eb3de82522bb26a05c3d86368af6794d7774ba276118`
  - `target-skeleton/README.md` = `beb17878287a31e48c3d2826ed01b3dade1bdf9cdcefa14b4b47b55bfb3930f7`
  - `docs/SKELETON-SMOKE.md` = `c75956785e80286a736e7fbbebccbbceea8e87047d1076388c00e9b69cdaea90`
  - `docs/TARGET-DESIGN.md` = `4803d6988743080598b49d841b264b4cb1ff7dec10590d7895109167cffb43b8`
  - `docs/TARGET-SKELETON.md` = `49b280946f57d025ffaf680e5c40e7c7c02fb4c50b8b32f35e359e21eb64738d`
- Shared files were **not** edited in the working tree of this session; only the new
  patches and this incoming directory are untracked additions.

---

## 2. Validation environment (host-only)

- Clone: `/home/shibbityshwab/openwrt-lab` (`openwrt main` `27663a0`, kernel
  `linux-6.18.60`, the tree's own OpenWrt GCC 14.4.0 toolchain, `CONFIG_WERROR=y` from
  the target config). `make target/linux/prepare` re-extracts the kernel, so the build
  below is canonical (full re-extract + full compile).
- Pristine tree for patch dry-runs: `linux-6.18.60` unpacked from the clone's
  `dl/linux-6.18.60.tar.xz`.
- dtschema **2026.9** in a throwaway venv (`/tmp/dtsv`); `yamllint` is NOT installed, so
  the tree's YAML-lint step is skipped with its own warning - the only validation step not
  exercised (same limitation as the fifth session).
- Evidence (logs, DTB, dry-run output, hashes) is local-only and gitignored:
  `build/linux-kbuild/waves/upstream-6.18-net-20261118/` (with `00-README.md`).

---

## 3. Exact commands and results

### 3.1 Pristine-6.18.60 patch dry-run - PASS

```
$ cd build/linux-kbuild/netport-pristine/linux-6.18.60
$ patch -p1 --dry-run < .../007-net-mdio-luofu.patch        -> RC=0
$ patch -p1 --dry-run < .../008-net-phy-luofu.patch         -> RC=0
$ patch -p1 --dry-run < .../009-net-gmac-luofu.patch        -> RC=0
```

(008's hunks report `offset -75` / `-12` on pristine because the patch was generated
against the clone's `drivers/net/phy/*`, which the OpenWrt generic patch set has extended;
on the clone's patched tree it applies at exact offsets. Cosmetic, no content difference.)

### 3.2 OpenWrt prepare - RC=0

```
$ cd /home/shibbityshwab/openwrt-lab
$ make target/linux/prepare V=s
Applying .../patches-6.18/007-net-mdio-luofu.patch using plaintext:
Applying .../patches-6.18/008-net-phy-luofu.patch using plaintext:
Applying .../patches-6.18/009-net-gmac-luofu.patch using plaintext:
RC=0
```

(also applies `001`-`005`; the tree is re-extracted, no `.rej`/`.orig` files).

### 3.3 Full kernel compile - RC=0, zero warnings

```
$ make target/linux/compile V=s -j16
  CC      drivers/net/mdio/mdio-luofu.o
  CC      drivers/net/phy/luofu-phy.o
  CC      drivers/net/ethernet/hisilicon/luofu-gmac.o
...
  Kernel: arch/arm/boot/zImage is ready
time: target/linux/compile#1284.97#81.76#145.33
RC=0
```

- `arch/arm/boot/zImage`: **3,223,728 B**, sha256
  `2749c294fe95b6fe3ece2ecc26357dbb44d0aa5e5fc765699e8865f0a6e5b6f5`.
  (The zImage embeds the build timestamp/UTS banner, so the hash is build-time dependent -
  the same pre-existing caveat as the earlier sessions; the object files are
  byte-stable for the same sources.)
- new objects built: `mdio-luofu.o` 33,216 B, `luofu-phy.o` 20,328 B,
  `luofu-gmac.o` 25,596 B.
- kernel `.config`: `CONFIG_LUOFU_MDIO=y`, `CONFIG_LUOFU_PHY=y`,
  `CONFIG_LUOFU_GMAC=y` (plus the existing `CONFIG_LUOFU_CRG/WDT=y`,
  `CONFIG_OF_MDIO=y`, `CONFIG_MDIO_BUS=y`, `CONFIG_PHYLIB=y`, `CONFIG_WERROR=y`).
- all three are built-in and linked: `System.map` has `luofu_mdio_probe` (c0b9b660),
  `luofu_gmac_probe` (c0b9b988), `luofu_genius_phy_drivers` (c1186840).
- **0 warnings in the whole compile log** (`grep -c 'warning:' = 0`); no new file is
  named in any diagnostic.
- `TEXT_OFFSET` contract unchanged: both `arch/arm/kernel/.head.o.cmd` and
  `arch/arm/boot/compressed/.head.o.cmd` carry `-DTEXT_OFFSET=0x00608000`;
  `System.map` gives `_text = stext = 0xc0608000` and
  `swapper_pg_dir = 0xc0604000` -> load/entry `(0x83200000 & 0xf8000000) + 0x00608000 =
  0x80608000`, i.e. the image recipe is unchanged.

### 3.4 Board DTS -> DTB with the patched kernel's own dtc - RC=0, zero warnings

```
$ cpp -nostdinc -I <kernel>/include -I <kernel>/arch/arm/boot/dts -undef \
      -D__DTS__ -x assembler-with-cpp -o luofu-r116-preprocessed.dts \
      target/linux/hisilicon/dts/luofu-r116.dts
$ <kernel>/scripts/dtc/dtc -I dts -O dtb -o luofu-r116.dtb luofu-r116-preprocessed.dts
RC=0, stderr EMPTY (0 bytes)
```

- DTB: **9,459 B**, sha256
  `f6781920eedc8421ffbe5d72aa6adecc9f64509ee2316f62263aabd5f4f9c887`.
- `fdtget` node checks: `/mdio@14000000` compatible `"hisilicon,luofu-mdio"`, seven
  `resets` entries (`0x30` word; bits 0x6, 0x1a..0x1f) and reset-names
  `"mdio gephyall gephy0 gephy1 gephy2 gephy3 gephy4"`; `/ethernet@14480000`
  compatible `"hisilicon,luofu-gmac"`, `status = "disabled"`, two `resets` entries
  (bits 0x13/0x14).
- 17 reset specifiers exist in the DTS; all are `(0x30, bit <= 31)`, which the CRG reset
  provider's xlate accepts (only the `0x2c/0x30/0x34` words, bit 0..31); independent
  Python re-read: `invalid: []`.

### 3.5 `dt_binding_check` for both new YAMLs - RC=0

```
$ PATH=<dtschema-venv>/bin:$PATH make ARCH=arm dt_binding_check \
      DT_SCHEMA_FILES="Documentation/devicetree/bindings/net/hisilicon,luofu-mdio.yaml:Documentation/devicetree/bindings/net/hisilicon,luofu-gmac.yaml"
  DTEX    .../hisilicon,luofu-mdio.example.dts
  DTC [C] .../hisilicon,luofu-mdio.example.dtb
  DTEX    .../hisilicon,luofu-gmac.example.dts
  DTC [C] .../hisilicon,luofu-gmac.example.dtb
RC=0
```

`yamllint` unavailable (noted above). A **full-draft-DTS `dtbs_check` is not clean**
and is not claimed - every other node still has a TODO binding.

### 3.6 Static checks

```
$ scripts/checkpatch.pl --no-tree -f drivers/net/mdio/mdio-luofu.c
  total: 0 errors, 0 warnings, 211 lines checked
$ scripts/checkpatch.pl --no-tree -f drivers/net/phy/luofu-phy.c
  total: 0 errors, 0 warnings, 52 lines checked
$ scripts/checkpatch.pl --no-tree -f drivers/net/ethernet/hisilicon/luofu-gmac.c
  total: 0 errors, 0 warnings, 137 lines checked
```

- No `request_mem_region()` / `ioremap()` / `devm_ioremap()` in any of the three
  drivers (MMIO only through `devm_platform_ioremap_resource()` / the MDIO bus API).
- No lab/bench breadcrumbs or private-repository references: `grep -i 'lab|bench'`
  over the three drivers and both YAMLs -> **no matches**; no `0xc0de*`/`0x10100*`
  diagnostic-cell writes, no vendor `hsan,*` strings, no vendor-only DT properties.
- The GEMAC driver registers no `net_device` and contains no `ndo_start_xmit`; probe
  returns `-EOPNOTSUPP` before any register access.
- Config consistency: 281 lines of the committed delta were compared with the compiled
  kernel's `.config`; all `LUOFU`/network symbols match. Two mismatches are a
  **pre-existing, unrelated** `CONFIG_DEVTMPFS`/`CONFIG_DEVTMPFS_MOUNT` pair (delta says
  `=y`, the OpenWrt generic config and the built `.config` say not-set). This session
  neither introduced nor fixed it; it does not touch the network symbols and is flagged
  for whoever regenerates the delta canonically.

---

## 4. Proven vs unverified (evidence grading)

**Proven, host-only (this session):**

- the three patches apply to pristine 6.18.60 and to the OpenWrt platform-patch order;
- the full 6.18 kernel compiles with the delivered patches and `config-6.18` under
  `CONFIG_WERROR=y` with **zero warnings**, and all three drivers are built-in and
  linked (System.map);
- both YAML bindings pass `dt_binding_check` and their examples compile/validate;
- the board DTS compiles with the patched kernel's dtc with zero warnings and the new
  nodes/reset cells are internally consistent (CRG xlate range check);
- `checkpatch` is clean on all three files; no vendor/lab instrumentation is embedded;
- `TEXT_OFFSET` and the `0x80608000` load/entry contract are unchanged.

**Unverified (no hardware run; do not read the port as working on silicon):**

- the 6.18 MDIO driver has not performed a transfer. The register model and handshake
  have hardware evidence only through the *previous out-of-tree 5.10 driver* (which read
  real C22 PHY IDs, e.g. `0x20669a09`); the CRG reset polarity is still unverified, so
  the first hardware run must confirm the bus comes out of reset and the PHYs enumerate.
- the 6.18 PHY driver has not trained a link. Autonegotiation/link/suspend behaviour and
  whether the vendor per-PHY firmware patch (`lsw/hi_ephy_%xv%x.bin`) is required are
  open; the generic C22 path is the honest baseline.
- the GEMAC: **there is no data path at all** - no ring, no DMA, no NAPI, no netdev; the
  DTS nodes are disabled and probe refuses. Nothing about MAC/switch/DMA behaviour is
  claimed.
- no boot, no image, no install path, no bench run. Publication remains **NOT READY**.

---

## 5. Remaining blockers / next steps

1. **MDIO/PHY bench validation:** boot a 6.18 image (blocked by the target image/install
   path), confirm reset release and PHY enumeration (addresses 2..5), then link/autoneg on
   the wired port; measure the MDC rate and decide whether PHY interrupts exist.
2. **GEMAC data path:** decode the TX/RX ring + DMA model from a citable source (vendor
   GPL source or a documented RE record), implement NAPI TX/RX with DMA mapping, program
   the MAC port mode from the phylink link-up callback, then enable the DTS nodes and
   attach via `phylink` (`CONFIG_PHYLINK=y` and MAC ops enter then). This is a
   multi-session lane, not a bug fix.
3. **Genius PHY firmware patch:** decode `hi_ephy_%xv%x.bin` only if hardware tests show
   the generic C22 path is insufficient.
4. **Upstream review items:** maintainer identity in the two YAMLs (currently the git
   identity placeholder), whether the seven-reset array is the right modelling for the
   MDIO bus node (vs per-PHY resets), the MDIO driver location, and whether a GEMAC
   binding should land before its data path exists (this draft keeps it because the board
   DT already carries the nodes and the partial driver documents the contract).

---

## 6. Notes for the lead (apply/commit)

- Apply: `cd opensource && git apply target-skeleton/docs/port-notes/net/shared.patch`
  (verified `git apply --check` RC=0), then commit the three new patches, this
  `incoming/net/` directory and the shared-file changes together. Do **not** apply the
  patch twice.
- **Possible conflict with the parallel FMC lane:** `shared.patch` touches the docs
  (`target-skeleton/README.md`, `docs/SKELETON-SMOKE.md`, `docs/TARGET-DESIGN.md`) that
  the FMC incoming patch also edits. No kernel source overlaps (FMC owns patch `006`,
  this lane `007`-`009`). If the FMC patch landed first, use `git apply -3` or merge the
  doc hunks by hand (both sides only add rows/sections; a union is the likely resolution).
- The clone `/home/shibbityshwab/openwrt-lab` was used for this lane's prepare/compile;
  `/home/shibbityshwab/openwrt-lab-fmc` was not touched.
- No shared repository file was edited in place; `git status` in `opensource/` shows only
  the three new patch files and this incoming directory as untracked additions
  (`docs/GPL-REQUEST.md` was already modified before this session - not mine).
