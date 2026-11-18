# incoming/pinctrl - peripheral pinctrl port summary (patch 011)

> Integration note: the lead applied `shared.patch` to the repository working tree on
> 2026-11-18 while this SUMMARY was being written. The applied state was re-verified:
> the pinctrl DTS block is byte-identical to the intended block and the integrated DTS
> (thermal + pinctrl) still compiles with zero dtc warnings. Patch `011` is in
> `patches-6.18/`; this SUMMARY remains the durable record.

**Task:** task-19 (parallel port lanes, agent `driver-pinctrl`). **Date:** 2026-11-18.
**Scope:** port the transcribed HiSilicon Hi5671Y "luofu" peripheral pinctrl
(`hi_kpinctrl` tables) to a 6.18 mainline pinctrl driver with its own compatible and
YAML binding, a Kconfig/Makefile symbol, and DTS/config integration, validated host-side
in an **isolated copy** of the `openwrt main` clone. **No device was touched** (no
router, no UART, no TFTP, no flash, no bench tool). Host-only, evidence-graded.

This directory is an **incoming** deliverable: it carries no shared-file edits directly.
Apply `shared.patch` and add the patch file that is already in `patches-6.18/`.

---

## 1. Deliverables

### 1.1 New kernel patch (already in the repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/011-pinctrl-luofu.patch` | `700bba855492352afc88dfaeca618c5f3a351bfec1734cdc294f54427fad7442` |

Numbering: `011` follows the accepted `001`-`010`. The patch is a bare unified diff
(`--- a/...` / `+++ b/...`, no `diff --git`) in the style of `004`-`010` and applies with
`patch -p1` in the OpenWrt platform-patch order. It touches six files (2 modified,
4 new, 1021 lines):

- `drivers/pinctrl/Kconfig`: `source "drivers/pinctrl/hisilicon/Kconfig"`.
- `drivers/pinctrl/Makefile`: `obj-y += hisilicon/`.
- `drivers/pinctrl/hisilicon/Kconfig` (new): `CONFIG_PINCTRL_LUOFU`
  (`bool`, `depends on ARCH_LUOFU || COMPILE_TEST`, `depends on OF`,
  `select PINMUX`, `select PINCONF`, `select GENERIC_PINCONF`).
- `drivers/pinctrl/hisilicon/Makefile` (new).
- `drivers/pinctrl/hisilicon/pinctrl-luofu.c` (new, 848 lines): platform driver on the
  mainline pinctrl framework: `pinctrl_register` through
  `devm_pinctrl_register()`; `pinctrl_ops` + `pinmux_ops` + `pinconf_ops`;
  `pinconf_generic_dt_node_to_map_all()` for the standard `function`/`groups`
  (and pin-configuration) state nodes.
- `Documentation/devicetree/bindings/pinctrl/hisilicon,luofu-pinctrl.yaml` (new, 122
  lines): the `hisilicon,luofu-pinctrl` node with the two documented `reg` windows
  (`mux`, `cfg`), the 37 pin names, the 24 group names, the 24 function names, and the
  generic state-node form.

### 1.2 `shared.patch` (apply to the repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/docs/incoming/pinctrl/shared.patch` | `5ffdb64ae66f6bb68553e11365712faf0a28dac58c67c1ebc8aad5966eb9ba02` |

- 69 lines, two files, **git-style** (`diff --git a/... b/...`).
- Verified: `git apply --check` RC=0 against the pre-application working tree; applying it
  in a throwaway copy reproduces the intended files byte-for-byte.
- Files and what they carry:
  - `target-skeleton/target/linux/hisilicon/luofu/config-6.18`: adds
    `CONFIG_PINCTRL_LUOFU=y` as an `[N]` entry with a provenance comment, immediately
    after the existing `CONFIG_PINCTRL=y` line. The driver's `select`s
    (`PINMUX`, `PINCONF`, `GENERIC_PINCONF`) are already resolved to `=y` in the
    committed delta, so no dependency line changes.
  - `target-skeleton/dts/luofu-r116.dts`: rewrites the disabled pinctrl placeholder
    (`pinctrl_peri: pinctrl@14900100`) into the real node: documented binding/driver,
    `status = "okay"`, unchanged windows; states are deliberately **not** attached
    (see section 5). Nothing else in the DTS is touched.
- Post-apply hashes, integrated working tree (HEAD + thermal lane + this lane), for the
  lead's sanity check:
  - `target-skeleton/dts/luofu-r116.dts` = `0471ceac395cb46c8dc9fbc0c37ee7615327e3bb68bbc958936c966f81cf8220`
  - `target-skeleton/target/linux/hisilicon/luofu/config-6.18` = `9332603399b9be0129b12d4524de4c5f518637345089d058ab32b1c4e16e8eb5`
- Shared files were **not** edited in the working tree by this session; only the new
  patch and this incoming directory are the session's own additions (the two modified
  shared files in `git status` are the lead's integration of the incoming patches).

---

## 2. What the driver does (and the evidence behind every register/bit)

The tables are transcribed from the vendor pin controller for this SoC, the only
register-level description of the block. Before porting, the tables were re-extracted
read-only from the local vendor module fixture (`hi_kpinctrl.ko`, `vermagic 5.10.201
SMP mod_unload ARMv7`, 17,636 B) with pyelftools + capstone, so the port is checked
against the binary rather than against the older lab transcription alone:

- **37 pins** (`pin0`..`pin36`): pin descriptor array, `.data+0x428`.
- **24 groups** with the real per-group pin lists (sfc, uart1, uart0, mdio1, spi, i2c,
  rgmii0, eth_act_led0..4, led0_0/led1_0/led0_1/led1_1, clk_test, avs, phy_clk, usb,
  jtag_gpio2..5), group table `.data+0x68`, 40-byte entries; `jtag_gpio2..5` also carry
  a group-level mux RMW (mask `0x1`, value `0x1` in mux byte offset `0x8`).
- **24 functions**, exactly one group each (1:1, function i <-> group i), function
  table `.rodata+0x20c` + group-name array `.data+0x5e4`.
- **per-pin mux data**: 5 x `{register byte offset, bit, function name}` per pin, pin i
  at `.data+0xf44 - i*0x40`; the mux window is `0x14900100`..`0x1490013c` (15 words).
  `set_mux` clears the hit's bit and sets it only when the hit names the requested
  function; `"null"` hits therefore stay clear; shared hits exist (e.g. pins 10/11 are
  both `mdio1` on mux word 16 bit 21, many pins share mux word 12 bit 1). A function is
  rejected for a group whose pins do not implement it, before any write.
- **pin configuration**: one word per pin in the `0x14940000`..`0x149400ff` window, at
  `4 * pin` (the vendor per-pin table's `cfg_off` is verified to be `4*i` for all 37
  pins). The vendor `luofu_pin_config_set` / `_get` implement six generic parameters as
  read-modify-writes of bit fields; this driver implements exactly those six and
  rejects every other generic parameter with `-EINVAL`:

  | cfg bit field | generic parameter (6.18 name) | vendor parameter (5.10 enum) |
  | --- | --- | --- |
  | `[0]` | `PIN_CONFIG_BIAS_PULL_UP` | `PIN_CONFIG_BIAS_PULL_UP` (5) |
  | `[1]` | `PIN_CONFIG_BIAS_PULL_DOWN` | `PIN_CONFIG_BIAS_PULL_DOWN` (3) |
  | `[4:2]` | `PIN_CONFIG_DRIVE_STRENGTH` | `PIN_CONFIG_DRIVE_STRENGTH` (9) |
  | `[5]` | `PIN_CONFIG_INPUT_SCHMITT_ENABLE` | `PIN_CONFIG_INPUT_SCHMITT_ENABLE` (14) |
  | `[7]` | `PIN_CONFIG_INPUT_ENABLE` | `PIN_CONFIG_INPUT_ENABLE` (12) |
  | `[10]` | `PIN_CONFIG_LEVEL` | `PIN_CONFIG_OUTPUT` (17) |

  The drive-strength field is a raw 3-bit setting: the vendor driver passes the generic
  argument straight into the field and no mA-per-code calibration is known, so the
  driver documents the argument as the raw field value. The `[10]` bit is the vendor
  parameter for driving a value on a pin; the parameter was renamed/redefined to
  `PIN_CONFIG_LEVEL` in this kernel, and that rename (identical documented semantics)
  is the one inferred mapping in the table. Group pin-configuration ops iterate the
  group's pins (vendor `hi_pin_config_group_set` behaviour); group get reports the
  first pin's value (vendor `hi_pin_config_group_get` behaviour) because the register
  is per pin.

### 2.1 Design decisions

1. **Probe performs no register access.** The vendor probe allocates state, obtains the
   two register resources named `mux`/`cfg`, takes an optional reset named `mux`,
   deasserts it and calls `pinctrl_register()` - no mux/cfg word is written. On this
   board the devicetree gives the node no reset specifier, so even the optional reset
   resolves to nothing. The port therefore maps the two windows (no read, no write),
   registers the controller and stops. A state is only programmed when a consumer asks
   for it through the standard pinctrl interface. This is deliberate: the vendor's
   "load-bearing" initial pin state is not known ([U]); writing a guessed default
   could remux the console or the boot flash.
2. **Two windows, one node, no syscon.** The block is not a shared page: it owns
   `0x14900100` (mux) and `0x14940000` (cfg) exclusively, so the node declares both
   `reg` windows and the driver claims them with
   `devm_platform_ioremap_resource_byname()`. No reset/clock cells are invented (the
   vendor node has neither).
3. **No pinconf stubs.** The lab driver's pinconf setters were no-ops, which silently
   accepts configuration it does not apply. This port implements the six parameters the
   vendor code actually writes (section 2) and fails everything else, so a DT that uses
   an undecoded property fails loudly at state-apply time.
4. **Pinconf operations are RMW under one spinlock.** The driver serialises the
   mux/cfg read-modify-write sequences (the vendor code does not, but two concurrent
   state applications could otherwise lose a bit).
5. **Out of scope, documented in the file header:** the separate IOMUX/JTAG-select
   register described by a different vendor node; the vendor group-level power-source
   path (dead on this SoC: group field 0 for every group, callback unpopulated); the
   always-on control/efuse window at `0x40000000` (different block, not pinctrl).

---

## 3. Validation environment (host-only, isolated clone)

- **Isolated clone:** `/home/shibbityshwab/openwrt-lab-pinctrl`, created with
  `cp -a --reflink=auto /home/shibbityshwab/openwrt-lab /home/shibbityshwab/openwrt-lab-pinctrl`.
  The shared clone `/home/shibbityshwab/openwrt-lab` was **not modified**; the build
  cache of the isolated copy was kept.
- Tree: `openwrt main` `27663a0`, `linux-6.18.60`, the tree's own OpenWrt GCC 14.4.0
  toolchain, `CONFIG_WERROR=y` from the target config.
- Series under test: `patches-6.18/001`-`010` from the repository plus this lane's
  `011`. The parallel thermal lane's `012` was deliberately **excluded** from this
  validation (it was not part of this task's series at the time).
- Pristine tree for the patch dry-run: a separate `linux-6.18.60` extract from the
  clone's `dl/linux-6.18.60.tar.xz`.
- `dtschema` 2026.9 in a throwaway venv; `yamllint` is NOT installed, so the tree's
  YAML-lint step is skipped with its own warning - the only validation step not
  exercised (same limitation as the earlier sessions).
- Evidence (logs, decoded tables, DTB, dry-run output, hashes) is local-only and
  gitignored: `build/linux-kbuild/waves/upstream-6.18-pinctrl-20261118/`.

---

## 4. Exact commands and results

### 4.1 Pristine-6.18.60 patch dry-run - PASS

`patch -p1 --dry-run` of `001`-`010` plus `011` against a fresh `linux-6.18.60`
extract: `PRISTINE_DRYRUN_RC=0`, no `FAILED` line; 42 files checked (the observed output
word for a dry-run is `checking file`). Evidence: `pristine-dryrun.log`,
`pristine-dryrun.rc`.

### 4.2 OpenWrt prepare - RC=0

`make target/linux/prepare V=s`: RC=0, re-extracts the kernel and applies `001`-`011`
in order; `011` patches the six files listed above, no `.rej`/`.orig` files.
Evidence: `prepare.log`, `prepare.rc`.

### 4.3 Full kernel compile - RC=0, zero warnings

`make target/linux/compile V=s -j16`: RC=0, wall 1m55s. Evidence: `compile.log`,
`compile.rc`, `artifacts.txt`.

- new object built: `CC drivers/pinctrl/hisilicon/pinctrl-luofu.o`, **52,336 B**;
- `arch/arm/boot/zImage`: **3,252,544 B**, sha256
  `a4686a4144271134f7b5408f64d030c1a4fc08c9b7d9e0d697d022b90c2ba979`
  (the zImage embeds the build timestamp/UTS banner, so the hash is build-time
  dependent - the same pre-existing caveat as the earlier sessions);
- **0 `warning:` lines in the whole compile log**; no new file appears in a diagnostic;
- kernel `.config`: `CONFIG_PINCTRL_LUOFU=y`, `CONFIG_PINCTRL=y`, `CONFIG_PINMUX=y`,
  `CONFIG_PINCONF=y`, `CONFIG_GENERIC_PINCONF=y`, `CONFIG_WERROR=y`;
- driver built-in and linked: `System.map` carries `luofu_pinctrl_probe`
  (`c0ae3cd4`), `luofu_pinctrl_desc` (`c11816e8`), `luofu_pinctrl_driver`
  (`c1181680`), `luofu_pinctrl_driver_init` (`c101b414`);
- `TEXT_OFFSET` contract unchanged: `-DTEXT_OFFSET=0x00608000` in both
  `arch/arm/kernel/.head.o.cmd` and `arch/arm/boot/compressed/.head.o.cmd`;
  `System.map` gives `_text = stext = 0xc0608000` and
  `swapper_pg_dir = 0xc0604000` -> load/entry
  `(0x83200000 & 0xf8000000) + 0x00608000 = 0x80608000`.

### 4.4 Board DTS -> DTB with the patched kernel's own dtc - RC=0, zero warnings

With `shared.patch` applied to a copy of the board DTS:

- `cpp -nostdinc -I <kernel>/include -I <kernel>/arch/arm/boot/dts -undef -D__DTS__
  -x assembler-with-cpp` then the patched kernel's `scripts/dtc/dtc -I dts -O dtb`:
  RC=0, **stderr empty (0 bytes)**;
- DTB **9,887 B**, sha256
  `eddb76f5cd738c37701940f49b896ec9221ccf62b540f6d8c898fe45a70dc55f`;
- `fdtget` checks: `/pinctrl@14900100` compatible `"hisilicon,luofu-pinctrl"`,
  `reg = <0x14900100 0x3c 0x14940000 0x100>`, `reg-names = "mux cfg"`,
  `status = "okay"`, no child nodes (no consumer state is attached, see section 5).

**Integration check (after the lead applied both incoming shared patches):** the current
working-tree DTS compiles with zero dtc warnings and both the pinctrl node and the
thermal sensor read `status = "okay"`; the pinctrl block is byte-identical to the
intended one. Evidence: `luofu-r116-integrated.dtb`,
`luofu-r116-integrated.dtc-stderr.txt` (0 bytes), `post-apply-hashes-integrated.txt`.

### 4.5 `dt_binding_check` for the new YAML - RC=0

`make ARCH=arm dt_binding_check DT_SCHEMA_FILES=Documentation/devicetree/bindings/pinctrl/hisilicon,luofu-pinctrl.yaml`:
RC=0; the example is extracted and compiled
(`DTEX .../hisilicon,luofu-pinctrl.example.dts`,
`DTC [C] .../hisilicon,luofu-pinctrl.example.dtb`). `yamllint` unavailable (noted above).
A **full-draft-DTS `dtbs_check` is not clean** and is not claimed - every other node
still has a TODO binding. Evidence: `dt-binding-check.log`.

### 4.6 Config derivation idempotent - PASS

With `011` applied in the isolated clone:

- `yes '' | make kernel_oldconfig -j16`: RC=0 (twice), emits a 298-line delta that
  contains `CONFIG_PINCTRL_LUOFU=y`;
- the two runs are **byte-identical** (`IDEMPOTENT_BYTE_IDENTICAL=YES`);
- after stripping comments, the re-derived symbol set is **identical** to the committed
  annotated `luofu/config-6.18` + this lane's `[N]` line
  (`CONFIG_SYMBOLS_IDENTICAL=YES`, 290 symbols);
- the clone's annotated target config was restored afterwards.
Evidence: `config-rederivation-{1,2}.log`, `config-6.18-annotated`,
`config-6.18-rederived-1`, `annot.syms`, `rederived.syms`.

### 4.7 Static checks

- `scripts/checkpatch.pl --no-tree --strict -f` on `pinctrl-luofu.c`: **total: 0
  errors, 0 warnings, 0 checks, 848 lines**; on the YAML: **0 errors, 0 warnings, 122
  lines**;
- no `lab`/`bench`/`hsan,`/`0xc0de` breadcrumb or vendor string in the new driver or
  the YAML (`grep -rni` -> no matches);
- MMIO only through `devm_platform_ioremap_resource_byname()` (2 calls); no direct
  `ioremap()` / `request_mem_region()` / `platform_get_resource()`;
- probe body contains no `readl` / `writel` (grep in the probe function: the only
  register accesses are in `set_mux`/pin-config ops).

---

## 5. Proven vs unverified (evidence grading)

**Proven, host-only (this session):**

- the vendor tables were re-extracted from the local module fixture and match the lab
  transcription: 37 pins, 24 groups with the real pin lists, 24 functions (1:1), the
  per-pin 5-hit mux table, the group-level mux fields, and `cfg_off = 4*pin`;
- the vendor mux/pinconf algorithms and the six pin-config bit fields were decoded from
  the module's own `set_mux` / `pin_config_set` / `pin_config_get` code, not guessed;
- the vendor probe writes no mux/cfg register, which is why this driver's probe does
  not either;
- `011` applies to pristine 6.18.60 and in the OpenWrt platform-patch order, and the
  full 6.18 kernel compiles with `001`-`011` and `config-6.18` under `CONFIG_WERROR=y`
  with **zero warnings**, the driver built-in and linked;
- the YAML binding passes `dt_binding_check` and its example compiles;
- the board DTS compiles with the patched kernel's dtc with zero warnings and the
  pinctrl node matches the binding's documented windows;
- `checkpatch` is clean on the driver and the binding; no lab/vendor breadcrumbs;
- the load/entry contract (`TEXT_OFFSET = 0x00608000`, `0x80608000`) is unchanged;
- the canonical config re-derivation is symbol-identical and idempotent.

**Unverified (no hardware run; do not read the port as working on silicon):**

- the driver has never been probed on hardware; no state has ever been applied;
- **the vendor's initial/probe-time pin state is [U].** Which mux/cfg state makes the
  MDIO/Ethernet path work without the vendor module is not established; the driver
  deliberately writes nothing at probe and no consumer state is wired in the board DTS;
- **consumer states are [U].** The vendor devicetree attaches no pinctrl state to any
  device node (its modules look states up by name), so which groups the MDIO, GEMAC,
  FMC and UART nodes must select is not established. The table does contain the
  `mdio1` (pins 10/11) and `rgmii0` (pins 12-23) functions; adding states without that
  measurement would be a guess;
- the MDIO two-state-bang question (`mdio1_mdc1` / `mdio1_mdc2`) remains open: the
  landed MDIO driver is MMIO-based and has no pinctrl consumer;
- the pin configuration electrical semantics are only as good as the vendor mapping: the
  drive-strength field has no known mA mapping, and the `[10]` bit is mapped to
  `PIN_CONFIG_LEVEL` by the documented parameter rename (an inference, marked in the
  driver header);
- whether a wrong mux on the console/NAND pins can make the box unreachable is exactly
  the risk that keeps this driver from touching anything at probe; no boot, no image, no
  install path, no bench run. Publication remains **NOT READY**.

---

## 6. Remaining blockers / next steps

1. **Bench: read-only window comparison** (needs a 6.18 boot path, which does not exist
   yet): read back `0x14900100` / `0x14940000` on the vendor kernel and compare the
   vendor module's applied state with the bootloader state; that closes the load-bearing
   question and decides the default state policy.
2. **Consumer-state decision** after (1): attach the minimum states to the MDIO/GEMAC/
   FMC/UART nodes only with that measurement; decide the MDIO two-state question.
3. **GPL source would close the remaining pinconf ambiguity** (GPL-REQUEST item 2,
   `hi_kpinctrl` sources): the bit-field decode is from the module, not the source; a
   source drop would confirm the six fields and the output-enable/level split.
4. **Upstream review items:** whether a new `drivers/pinctrl/hisilicon/` directory is the
   right home (no Hisilicon pinctrl drivers remain in 6.18), whether the compatible
   should be `hisilicon,luofu-pinctrl` or SoC-prefixed, and the usual binding naming
   reviews.

---

## 7. Notes for the lead (documentation updates this lane did not make)

Shared docs are the lead's to update (per the workspace protocol). Suggested edits:

- `target-skeleton/README.md`: add a row for `patches-6.18/011`; the intro paragraphs
  ("fifth/seventh session") gain an eighth-session note; the two "blocks that still have
  no driver" lists (around lines 301 and 380) should drop pinctrl.
- `docs/SKELETON-SMOKE.md`: add the 2026-11-18 eighth session (the section 4 results
  above, in the existing style).
- `docs/TARGET-DESIGN.md`: 6.2 row for peripheral pinctrl (around line 490) is now
  implemented with compatible `hisilicon,luofu-pinctrl` and no separate dt-bindings
  header needed; 7.1 and the "still unwritten" wording (around lines 518, 683, 697) need
  the same update; note the pinconf decode in the driver inventory.
- `docs/TARGET-SKELETON.md` (around line 22): drop pinctrl from the missing series.
- `target-skeleton/dts/luofu-r116.dts` header: the driver-list sentence still says
  `patches-6.18/004-010`; after both 011 and the thermal lane's 012 land it should be
  `004-012`.
- `config-6.18` header legend: the `[N]` range `(patches-6.18/004-010)` is stale (now
  004-011, or 004-012 with thermal); the canonical re-derivation record can add that a
  re-derivation with `011` applied was verified symbol-identical and idempotent.

Conflicts: none with the thermal lane - its DTS hunk (thermal block) and config hunk
(after the FMC block) are disjoint from this lane's (pinctrl node, `CONFIG_PINCTRL`
area), and both orders were checked before the lead applied them.

Evidence: `build/linux-kbuild/waves/upstream-6.18-pinctrl-20261118/` (local-only,
gitignored; includes the vendor-module table extraction and disassembly used as
evidence, the validation logs, DTS/DTB, config runs and hashes).
