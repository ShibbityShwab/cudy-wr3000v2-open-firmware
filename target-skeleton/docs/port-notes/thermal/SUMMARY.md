# incoming/thermal - thermal sensor + zone (patch 012) summary

**Task:** task-20 (parallel port lanes, agent `driver-thermal`). **Date:** 2026-11-18.
**Scope:** implement the HiSilicon Hi5671Y "luofu" temperature sensor as a mainline
of-thermal driver, with its YAML binding, Kconfig/Makefile entry, config symbol and board
thermal zone via `shared.patch`. v1 is **sensor + zone only**: no cooling map, no
threshold/interrupt/trim programming. Driver + binding + Kconfig/Makefile + DTS/config
integration, validated host-side in an isolated copy of the OpenWrt clone.
**No device was touched** (no router, no UART, no TFTP, no flash, no bench tool).
Host-only, evidence-graded.

This directory is an **incoming** deliverable: it carries no shared-file edits directly.
Apply `shared.patch` and add the patch file that is already in `patches-6.18/`.

---

## 1. Deliverables

### 1.1 New kernel patch (already in the repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/012-arm-thermal-luofu-tvsensor.patch` | `3c42f8e6a08ea159b5c3a074890ff64d8c1476f3cbc16bc7c92780b89c7edb5f` |

Numbering: `012` follows the merged `001`-`010` (patch `011`, peripheral pinctrl, is a
parallel lane; this port does not touch it). The patch is a bare unified diff
(`--- a/...` / `+++ b/...`, no `diff --git`) in the style of `004`-`010` and applies with
`patch -p1` in the OpenWrt platform-patch order.

`012` adds:

- `drivers/thermal/luofu_tvsensor.c` (new): the temperature read path
  (`->get_temp` only) registered with `devm_thermal_of_zone_register()`; **no register
  writes anywhere in the driver**;
- `drivers/thermal/{Kconfig,Makefile}`: `CONFIG_LUOFU_TVSENSOR` (`tristate`, `depends on
  ARCH_LUOFU || COMPILE_TEST`, `HAS_IOMEM`, `OF`);
- `Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml` (new): the
  `hisilicon,luofu-tvsensor` node, `#thermal-sensor-cells = <0>`, no interrupt and no
  clocks/resets; the description records the register-offset window requirement.

### 1.2 `shared.patch` (apply to the current repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/docs/incoming/thermal/shared.patch` | `fa7195eaeb12b0ae8ea1944a84727b5ad8c2da552d2ad7b333999354550aad35` |

Files and what they carry:

- `target-skeleton/dts/luofu-r116.dts`: rewrites the thermal placeholder block into the
  enabled `hisilicon,luofu-tvsensor` node (reg widened from the vendor `0x14` to
  `0x100`, vendor interrupt SPI 0 deliberately not claimed) plus the `thermal-zones/cls0-thermal`
  zone with the vendor trips (115 C passive / 125 C critical, 3 C hysteresis,
  `polling-delay = <1000>`) and **no cooling-maps**.
- `target-skeleton/target/linux/hisilicon/luofu/config-6.18`: adds
  `CONFIG_LUOFU_TVSENSOR=y` as an `[N]` entry with its provenance comment.

Post-apply hashes (sanity check for the lead):

- `target-skeleton/dts/luofu-r116.dts` = `b85afa1d7fc260c609aceda3d44dd0b959db0ea5bac486bbe48ae27afbf2aa4d`
- `target-skeleton/target/linux/hisilicon/luofu/config-6.18` = `a9db6e20357e55d08a7a355cb6e9983a934dade16c2d4bd82ca3a1b6aff2d644`

Shared files were **not** edited in the working tree of this session; only the new patch
and this incoming directory are untracked additions. The shared doc/header updates are left
to the lead because the parallel pinctrl lane (011) touches the same general areas;
suggested text is in section 6.

---

## 2. What the driver does

The sensor is one register block at `0x14900500`. The die temperature is the low 16 bits of
the word at offset `0x24`, converted exactly as the vendor driver's v4 path does (hwver 3 is
the only variant this board uses):

```
raw          = readl(base + 0x24) & 0xffff
millidegrees = ((165 * raw - 32835) / 100) * 1000 - 40000
```

The driver keeps the vendor arithmetic operation for operation, including the truncating
integer division (C and the vendor's ARM `sdiv` both truncate toward zero), so the reported
value matches the vendor path bit for bit. The formula is the one fact beyond doubt: it was
decoded from `hi_tsensor_hw_v4_get_temp` and verified numerically (raw 240 -> 27.0 C,
nominal slope 1.65 C/LSB). It is reproduced in the lab source
(`opensource/lab/luofu-thermal/luofu-thermal.c`) and in
`opensource/docs/REMAINING-DRIVERS.md` section 3.1.

Registration uses the 6.18 of-thermal API, `devm_thermal_of_zone_register()` with a
`struct thermal_zone_device_ops` (`.get_temp`), data retrieved by
`thermal_zone_device_priv()`. The zone itself, its trips and its `polling-delay = <1000>`
are board devicetree data; the driver does not create a zone and does not program a
threshold.

**Access discipline:** the driver has exactly one MMIO access, the `readl()` in
`->get_temp`. Probe maps the resource and registers the sensor and performs no register
access. There is no `writel`/`writew`/`writeb`/`iowrite`/`regmap_write` call in the
file, no interrupt handler, no `ioremap()` of a shared page and no clock/reset consumer.

---

## 3. Design decisions (and why)

1. **Mainline of-thermal sensor, not a private thermal interface.** The plan
   (`REMAINING-DRIVERS.md` 3.3) names `devm_thermal_of_zone_register` and the 6.18 tree
   renamed the old `thermal_zone_of_device_ops` path away: `include/linux/thermal.h` in
   6.18.60 has only `struct thermal_zone_device_ops` and
   `devm_thermal_of_zone_register(struct device *, int, void *,
   const struct thermal_zone_device_ops *)`. The driver therefore uses the current API
   (verified by building against that tree).
2. **Reg window widened from the vendor `0x14` to `0x100`.** The vendor devicetree declares
   a 20-byte window while its own driver reads `+0x24` (36 bytes in); the vendor's
   page-granular `devm_ioremap()` of the platform resource masked the mistake. A mainline
   driver must describe a window that covers every register it touches, so the node covers
   the containing 256-byte block and the binding states the real minimum (0x28). The exact
   extent of the block remains [U]; nothing beyond `+0x24` is touched.
3. **Polled, not interrupt-driven.** The vendor node has an interrupt (SPI 0) and the vendor
   module has `set_th`/`set_int_mask`/`clr_int` plus a critical-event work item, but no
   threshold or interrupt-mask offset/encoding has been decoded and no store to this block
   has ever been measured. The documented minimum is therefore the read path plus the
   vendor `polling-delay = <1000>`, and the node claims no interrupt. Claiming an IRQ the
   driver cannot service would be wrong.
4. **No enable, mode or trim writes.** The vendor module has `set_trim`, enable and
   `addition-en`/`efuse-trim-en` paths, but their register model is [U]; the read path needs
   no vendor enable sequence (the vendor kernel reads the sensor without re-enabling it, so
   the bootloader leaves it running). The binding does not carry the vendor
   `int-base-offset`, `trim`, `hwver`, `addition-en` or `efuse-trim-en` properties; hwver 3
   is implied by the compatible and recorded in the binding description. Probe only maps
   and registers.
5. **Sensor + zone, no cooling map.** The vendor cooling map points at
   `hisilicon,luofu-net-cdev`, whose throttle target is network rate limiting (LSW
   CAR/flow shaping). That model does not exist in this tree, so v1 keeps the zone and the
   vendor trips but drops the cooling map - per the plan, "leaving the map in place with no
   cooling device is a DT warning and a bad review smell". The passive trip therefore only
   produces thermal events; the critical trip runs the thermal core's default critical
   action (orderly shutdown), which is also what the vendor tripped on (the vendor's own
   action policy for the critical trip is not independently measured).
6. **`sustainable-power` dropped, `polling-delay-passive` dropped.** Both are vendor
   devicetree values whose units/meaning are [U] here: `sustainable-power` is consumed only
   by the power_allocator governor, which cannot be used without a cooling device, and the
   vendor `polling-delay-passive` value is not reliably decoded. Only the vendor
   `polling-delay = <1000>` is carried; the passive delay defaults to 0.
7. **Trip values are vendor policy data, not our measurement.** 115 C passive / 125 C
   critical / 3 C hysteresis are exactly the vendor tree's values; the SUMMARY and the DTS
   comment say so, and changing them needs bench data (section 5). The vendor's
   `trip-point@0`/`trip-point@2` node names become `trip-point0`/`trip-point1`: node names
   have no meaning beyond uniqueness, and `trip-point@1` was absent.
8. **No clocks/resets.** The vendor node has neither; whether the block has a gate or a
   reset Linux must manage is [U] and is stated as such in the binding and DTS. No reset
   path was invented (the CRG reset xlate would reject an out-of-range offset anyway).

---

## 4. Evidence grading

| claim | source in the repository | grade |
| --- | --- | --- |
| temperature = low 16 bits at `+0x24`; formula `((165*raw-32835)/100)*1000-40000` | decoded from `hi_tvsensor.ko` `hi_tsensor_hw_v4_get_temp`, numerically verified (raw 240 -> 27.0 C); `opensource/lab/luofu-thermal/luofu-thermal.c`; `REMAINING-DRIVERS.md` 3.1 | [V] |
| hwver 3 is this board's sensor variant; v4 read path is the one used | vendor DT `tvsensor@14900500: hwver = <3>`; vendor module has v1 (`+0x4`) and v4 (`+0x24`) paths | [V] |
| trips: 115000 passive, 125000 critical, hysteresis 3000, `polling-delay = <1000>` | vendor DT `thermal-zones/cls0` (`soc/luofu-r116-pinned.dts:1432-1468`) | [V] vendor policy data, not a measurement by us |
| vendor `sustainable-power = <2000>` exists and is omitted | same vendor node; units [U], only power_allocator consumes it, no cooling device in v1 | [V] property, [U] meaning |
| vendor interrupt SPI 0 and threshold/trim paths exist; semantics unknown | vendor DT `interrupts = <SPI 0>`; module symbols `hi_tsensor_hw_v4_set_th`, `set_trim`, `set_int_mask`, `clr_int`, `hi_tvsensor_irq_handler` | [V] existence, [U] register model |
| register window must cover `+0x24`; vendor DT `0x14` is too short | vendor DT `reg = <0x14900500 0x14>` vs the vendor driver's `+0x24` read | [V] mismatch; widened window is [I] |
| block needs no clock/reset from Linux | vendor DT has neither; the vendor kernel reads the block | [I], [U] whether Linux must manage either |
| a 0x100-byte window is "the block" | chosen as the containing 256-byte register block; the true extent is not documented | [I]/[U] |
| raw 0 / implausible values are handled by the thermal core, not clamped | 6.18 `__thermal_zone_get_temp`/`thermal_zone_device_update`: temps `<= THERMAL_TEMP_INVALID` are ignored and polling continues | [V] code path |

---

## 5. Validation (host-only, isolated clone)

All logs/artifacts are local-only (gitignored) under
`build/linux-kbuild/waves/2026-11-18-thermal/`; nothing there is committed. The validation
copy is `/home/shibbityshwab/openwrt-lab-thermal`; the shared
`/home/shibbityshwab/openwrt-lab` was **not** modified.

### 5.1 Patch and series integration

**1. Patch applies to pristine `linux-6.18.60`** (extracted from the clone's
`dl/linux-6.18.60.tar.xz`, sha256 `3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e`):

```text
$ cd build/linux-kbuild/waves/2026-11-18-thermal/a   # pristine extract
$ patch -p1 --dry-run -i ../012-arm-thermal-luofu-tvsensor.patch
checking file drivers/thermal/Kconfig
checking file drivers/thermal/Makefile
checking file drivers/thermal/luofu_tvsensor.c
checking file Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml
RC=0
```

A real apply in a throw-away copy reproduces the patched source tree byte-for-byte
(`diff -r b dryrun` -> no output), so the delivered patch and the diff basis are identical.

**2. OpenWrt prepare** on the isolated clone
(`/home/shibbityshwab/openwrt-lab-thermal`, series `001`-`010` plus `012` and the
`shared.patch` DTS/config edits):

```text
$ make target/linux/prepare V=s
Applying .../patches-6.18/012-arm-thermal-luofu-tvsensor.patch using plaintext:
patching file drivers/thermal/Kconfig
patching file drivers/thermal/Makefile
patching file drivers/thermal/luofu_tvsensor.c
patching file Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml
RC=0   (1,173 "patching file" lines over the generic + target patches; 0 .rej/.orig files)
```

**3. Full kernel compile** with the tree's own OpenWrt GCC 14.4.0
(`arm-openwrt-linux-muslgnueabi-gcc (OpenWrt GCC 14.4.0 toolchain-c0e63334e4) 14.4.0`),
no make-command-line override:

```text
$ make target/linux/compile V=s -j16
  CC      drivers/thermal/luofu_tvsensor.o
  ...
  Kernel: arch/arm/boot/zImage is ready
RC=0   (CPU 1205.093 s, wall 110.467 s)
```

- `arch/arm/boot/zImage`: **3,250,848 B**, sha256 `34ed611615a537c77bc38f2a36823500b38174a2d32419ba24ca6253932965bb`;
- **0 warning lines** in the whole compile log;
- the driver is built-in: `CONFIG_LUOFU_TVSENSOR=y` in the kernel `.config`,
  `luofu_tvsensor_probe`/`luofu_tvsensor_get_temp` in `System.map`, and
  `kernel/drivers/thermal/luofu_tvsensor.ko` in `modules.builtin`;
- the load-address contract is unchanged: `_text = stext = 0xc0608000` in `System.map` and
  `-DTEXT_OFFSET=0x00608000` in both early-boot `.cmd` files
  (`arch/arm/boot/compressed/.head.o.cmd`, `arch/arm/kernel/.head.o.cmd`);
- the delivered target config does **not** set `CONFIG_WERROR` (the kernel `.config` says
  `# CONFIG_WERROR is not set`), so the compile above is not warnings-as-errors. As a
  supplementary proof, the clone's target config was temporarily given `CONFIG_WERROR=y`,
  the kernel tree was removed and `prepare` + full `compile` were repeated from scratch:
  **RC=0, 0 warnings**, `_text = stext = 0xc0608000`, `-DTEXT_OFFSET=0x00608000`,
  zImage 3,250,848 B sha256 `34ed611615a537c77bc38f2a36823500b38174a2d32419ba24ca6253932965bb` (identical bytes). The
  submitted config was restored afterwards and does not carry the validation-only line.

### 5.2 Device tree and bindings

**4. Board DTS -> DTB with the patched kernel's own dtc:**

```text
$ cpp -nostdinc -I <kernel>/include -I <kernel>/arch/arm/boot/dts -undef -D__DTS__ -x assembler-with-cpp -o luofu-r116-preprocessed.dts dts/luofu-r116.dts
$ <kernel>/scripts/dtc/dtc -I dts -O dtb -o luofu-r116.dtb luofu-r116-preprocessed.dts
RC=0, stderr EMPTY (zero warnings)
```

- DTB: **9,799 B**, sha256 `2677acd784ef94bb22c764d24733dd4dcf53108bd26151af1410ca2dfa0694a1`;
- node checks: `/thermal-sensor@14900500` has compatible `"hisilicon,luofu-tvsensor"`,
  `reg = <0x14900500 0x100>`, `#thermal-sensor-cells = <0>`, `status = "okay"`;
  `/thermal-zones/cls0-thermal` has `polling-delay = <1000>` (0x3e8) and
  `thermal-sensors` pointing at the sensor phandle; `trip-point0` = 115000/3000/passive,
  `trip-point1` = 125000/3000/critical; no `cooling-maps` node.

**5. Device-tree binding validation:**

```text
$ PATH=<dtschema-venv>/bin:$PATH make ARCH=arm dt_binding_check DT_SCHEMA_FILES="Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml"
  DTEX    .../hisilicon,luofu-tvsensor.example.dts
  DTC [C] .../hisilicon,luofu-tvsensor.example.dtb
RC=0
```

dtschema **2026.9**. `yamllint` is not installed in the validation venv, so the tree's
YAML-lint step is skipped with its own warning - the only validation step not exercised
(same limitation as the earlier driver lanes). One unrelated pre-existing schema warning is
emitted while building `processed-schema.json`
(`net/realtek,rtl8152.yaml: realtek,led-data: missing type definition`); it is upstream
6.18.60 content, not this lane.

The real board DTB was also run through `dt-validate` against the processed schema:
RC=0 and **no message names the thermal sensor or zone**. The five remaining messages are
the pre-existing draft-DTS TODOs (sp804 compatible, four UART clock-names issues). Note that
the earlier restart-lane log had a `thermal-zones: 'cls0' does not match ...` message from
the then-current placeholder; the delivered zone node is `cls0-thermal` exactly because
`thermal-zones.yaml` requires child names to match `^[a-zA-Z][a-zA-Z0-9\-]{1,10}-thermal$`,
so that message is gone.

**6. checkpatch:**

```text
$ scripts/checkpatch.pl --no-tree -f drivers/thermal/luofu_tvsensor.c
total: 0 errors, 0 warnings, 133 lines checked
$ scripts/checkpatch.pl --no-tree -f Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml
total: 0 errors, 0 warnings, 56 lines checked
```

### 5.3 Config derivation

`yes '' | make kernel_oldconfig` was run twice in the clone with the new Kconfig in the
tree (patch `012` applied by `prepare`):

- run 1 and run 2: RC=0; the two emitted deltas are **byte-identical**
  (sha256 `dd491a47eab441786fd657449e5df54661f34c7ae14c1f1f31b0558fad1049f3`) - `IDEMPOTENT_BYTE_IDENTICAL`;
- the emitted symbol set is **identical** to the delivered annotated `config-6.18`
  (`CONFIG_SYMBOLS_IDENTICAL`, 298 symbols, after stripping comments),
  including `CONFIG_LUOFU_TVSENSOR=y`;
- the driver adds no resolved dependency line beyond its own symbol: `THERMAL`,
  `THERMAL_OF`, `OF` and `HAS_IOMEM` are already in the delta;
- the clone's target config was restored to the delivered annotated file afterwards.

### 5.4 Discipline scans

`60-static-checks.log`, on the driver and binding from the patched kernel tree:

- **no write path at all**: no `writel`/`writew`/`writeb`/`iowrite`/`regmap_write`/
  `memcpy_toio` matches; the only read is the single `readl()` in `->get_temp`;
- probe performs no register access: `devm_kzalloc` + `devm_platform_ioremap_resource` +
  `devm_thermal_of_zone_register` only;
- no `ioremap()`/`request_mem_region()`/`platform_get_resource()` (the 0x14900500 page has
  a single owner, the sensor driver, and is mapped through the device-managed helper);
- no lab/bench/private-repository breadcrumbs in the driver, binding or patch;
- the only compatible string is `hisilicon,luofu-tvsensor`; no vendor-only DT property
  names are read by the driver (the vendor properties are only named in a comments list of
  things that are deliberately not supported);
- `shared.patch` reverse-applies cleanly to the edited repo copy and forward-applies to a
  pristine `opensource/` copy with RC=0, producing files byte-identical to the intended
  post-edit versions (both files compared).

**7. Repository state:** only two untracked additions in `opensource/`: the patch
`012-arm-thermal-luofu-tvsensor.patch` and `target-skeleton/docs/incoming/thermal/`.
No tracked/shared file was modified in the working tree; `shared.patch` is the only path by
which the DTS and config change reach the repository.


---

## 6. Integration notes for the lead (shared docs; not edited here)

The workspace protocol reserves the shared docs; the parallel pinctrl lane (011) touches
the same files' general areas, so these are left to one integrator pass:

1. `target-skeleton/target/linux/hisilicon/luofu/config-6.18` legend line
   `#   [N]  new in the luofu driver series (patches-6.18/004-010):` -> `004-012` (or name
   011/012 explicitly).
2. `target-skeleton/dts/luofu-r116.dts` header: the driver-list paragraph still says
   `(patches-6.18/004-010)`; add "peripheral pinctrl (011) and the thermal sensor (012)".
3. `opensource/docs/SKELETON-SMOKE.md`: a session section for this lane. Suggested text is
   in section 7.
4. `opensource/docs/TARGET-DESIGN.md` section 7.1 thermal row: change "thermal sensor: no
   driver" to "host-compiled draft, patch 012: read path + of-thermal zone only; no
   threshold/IRQ/trim/cooling model".
5. `opensource/target-skeleton/docs/TARGET-SKELETON.md` driver-series status: add patch 012.
6. `opensource/target-skeleton/README.md`: add `012` to the patch list.

---

## 7. Suggested SKELETON-SMOKE session section (for the lead)

```markdown
## 2026-11-18 (eighth session): thermal sensor + zone (patch 012)

### What was added

Patch `012-arm-thermal-luofu-tvsensor.patch` adds
`drivers/thermal/luofu_tvsensor.c` (read-only temperature sensor registered with
`devm_thermal_of_zone_register()`), its Kconfig/Makefile entry
(`CONFIG_LUOFU_TVSENSOR`), and
`Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml`. The board DTS
placeholder block becomes a real sensor node (`reg` widened from the vendor `0x14` to
`0x100`, vendor interrupt SPI 0 not claimed) plus the `cls0-thermal` zone with the vendor
trips (115 C passive / 125 C critical, 3 C hysteresis, `polling-delay = <1000>`) and no
cooling-maps. The zone node is named `cls0-thermal` because `thermal-zones.yaml` only
accepts child names matching `*-thermal` (the old placeholder name `cls0` was a
dt-validate error inherited from the draft DTS).

### Design decisions (and why)

1. **The conversion is the vendor v4 path, reproduced exactly.** The driver reads
   `+0x24 & 0xffff` and computes `((165 * raw - 32835) / 100) * 1000 - 40000`
   millidegrees, operation for operation (including the truncating division), so its
   value matches the vendor kernel bit for bit. That formula is the decoded and
   numerically verified fact (raw 240 -> 27.0 C).
2. **Registry through the 6.18 of-thermal API.** The 6.18 tree only has
   `struct thermal_zone_device_ops` + `devm_thermal_of_zone_register()` (the old
   `thermal_zone_of_device_ops` interface is gone); the driver uses the current API and
   gets its data with `thermal_zone_device_priv()`.
3. **Reg window widened to `0x100`.** The vendor DT declares 0x14 bytes while its own
   driver reads +0x24; the vendor's page-granular ioremap masked that. The node covers the
   containing 256-byte block and the binding records the real minimum (0x28). The extent
   of the block is [U]; nothing beyond +0x24 is touched.
4. **Polled, no interrupt, no writes.** The vendor threshold/interrupt/enable/trim
   registers are not decoded and no store to the block has been measured, so the documented
   minimum is the read path, the vendor polling delay, and the zone. The vendor interrupt is
   not claimed; probe performs no register access; the driver has no write path.
5. **Sensor + zone, no cooling map.** The vendor cooling device is a network rate limiter
   (LSW CAR), out of scope until the LSW lane; the trips are vendor data, not our
   measurement, and the zone keeps them without a cooling device (the critical trip still
   runs the thermal core's default critical action). `sustainable-power` is dropped (units
   [U]; only the power_allocator governor consumes it, and it needs a cooling device).

### Verification (exact commands and results)

All logs/artifacts are local-only (gitignored) under
`build/linux-kbuild/waves/2026-11-18-thermal/`; nothing there is committed. The isolated
clone is `/home/shibbityshwab/openwrt-lab-thermal`; the shared
`/home/shibbityshwab/openwrt-lab` was not modified.

- patch applies to pristine `linux-6.18.60` (`patch -p1 --dry-run` RC=0, real apply
  byte-identical to the diff basis);
- `make target/linux/prepare V=s` in the clone with series `001`-`010` + `012` and the
  shared DTS/config edits: **RC=0**, 1,173 "patching file" lines, 0 `.rej`/`.orig`;
- `make target/linux/compile V=s -j16` with the tree's own OpenWrt GCC 14.4.0:
  **RC=0**, **0 warnings**, zImage **3,250,848 B** sha256
  `34ed611615a537c77bc38f2a36823500b38174a2d32419ba24ca6253932965bb`,
  `CONFIG_LUOFU_TVSENSOR=y`, driver symbols in `System.map`,
  `_text = stext = 0xc0608000`, `-DTEXT_OFFSET=0x00608000` unchanged;
  a validation-only clean rebuild with `CONFIG_WERROR=y` is also RC=0 / 0 warnings and
  produces the identical zImage (the delivered config keeps `# CONFIG_WERROR is not set`);
- `dt_binding_check` RC=0 for the new YAML (dtschema 2026.9; yamllint unavailable, that one
  lint step skipped; one unrelated pre-existing realtek schema warning);
- board DTS: patched-kernel dtc RC=0, **zero warnings**, DTB **9,799 B** sha256
  `2677acd784ef94bb22c764d24733dd4dcf53108bd26151af1410ca2dfa0694a1`; the sensor and zone
  nodes validate against their schemas and `dt-validate` has no message naming them (the
  five remaining messages are the pre-existing sp804/UART draft TODOs);
- `checkpatch`: 0 errors / 0 warnings on the driver (133 lines) and binding (56 lines);
- config delta re-derived with the new Kconfig: two `make kernel_oldconfig` runs RC=0,
  byte-identical (sha256 `dd491a47eab441786fd657449e5df54661f34c7ae14c1f1f31b0558fad1049f3`),
  298 symbols **identical** to the delivered annotated config, including
  `CONFIG_LUOFU_TVSENSOR=y`;
- probe has no register access, the driver has no write path at all, and no
  lab/bench/private breadcrumbs are present.

### What this proves and what it does not

Proven (host-only): both new files apply to pristine 6.18.60 and in the OpenWrt
platform-patch order, build into the board kernel as a built-in with zero warnings (also
under `CONFIG_WERROR=y` on a clean rebuild), pass `dt_binding_check` and `checkpatch`,
compile the board DTS with zero dtc warnings, leave the TEXT_OFFSET contract unchanged, and
cannot write hardware from probe.

Not proven / not claimed - any hardware behaviour: that reading `0x14900500 + 0x24` on this
board returns the converted temperature (the arithmetic is verified from the vendor module,
the register access is not measured by us); that the block is clocked/gated as the
bootloader leaves it; the threshold/interrupt/enable/trim register model; whether the
115/125 C trip policy is right for this board with no cooling device; whether the block
needs a clock or reset from Linux. **No boot, no bench, no device, no image: publication
NOT READY.**

### Remaining TODOs for the thermal port

1. One attended read-only bench comparison: our driver's
   `/sys/class/thermal/thermal_zone0/temp` against the vendor kernel's zone over the same
   window, recording the raw `+0x24` value with the converted one.
2. Decode the threshold/interrupt/enable/trim model (RE of `hi_tvsensor.ko` and/or GPL
   request item 2) before claiming an interrupt or implementing `.set_trips`.
3. Revisit trips and `sustainable-power` when the LSW/CAR cooling device exists.
4. Upstream review items: split the binding into its own patch (the series keeps them
   together like `004`-`010`), maintainer identity, and whether the vendor's vendor-only
   properties should ever be modelled.

Evidence: local-only `build/linux-kbuild/waves/2026-11-18-thermal/`.

```

---

## 8. What this proves and what it does not

Proven (host-only): the thermal driver and its binding are real code that apply to pristine
`linux-6.18.60` and in the OpenWrt platform-patch order together with the merged series
`001`-`010`; the driver builds into the board kernel as a built-in with zero warnings (the
delivered config does not set `CONFIG_WERROR`; a validation-only clean rebuild with it is
also clean); the binding passes `dt_binding_check`; the board DTS
compiles with zero dtc warnings and the thermal nodes validate; `checkpatch` is clean;
probe cannot write a register; and the load-address/TEXT_OFFSET contract is unchanged.

Not proven / not claimed - any hardware behaviour:

- that reading `0x14900500 + 0x24` on this board returns the vendor conversion's range
  (the arithmetic is verified from the module; the register access is not measured by us);
- that the block is clocked/gated as the bootloader leaves it when our kernel boots;
- the threshold/interrupt/enable/trim register model (not decoded; no writes implemented);
- whether the 115 C/125 C/3 C trip policy is right for this board with no cooling device -
  the values are the vendor's, not our measurement;
- whether the sensor needs a clock or reset from Linux;
- no boot, no bench, no device, no image: **publication remains NOT READY**.

---

## 9. Remaining TODOs

1. One attended (read-only) bench comparison: boot a kernel with this driver, read
   `/sys/class/thermal/thermal_zone0/temp` and compare with the vendor kernel's zone over
   the same time window; record the raw register value and the converted value together.
2. Decode the threshold/interrupt/enable/trim register model (RE of `hi_tvsensor.ko` and/or
   GPL request item 2) before claiming an interrupt; only then add `interrupts` to the
   binding and `set_trips` to the driver.
3. Revisit the trip table and `sustainable-power` once the LSW/CAR cooling device exists.
4. Upstream review items: the DT binding and driver would normally be separate patches
   (this series keeps them together like `004`-`010`); maintainer identity; whether the
   vendor-style single-register sensor should instead be a generic
   `thermal-mmio`-like node (it is not: the conversion is a signed linear formula with an
   offset, which `thermal_mmio.c` cannot express).

Evidence: local-only `build/linux-kbuild/waves/2026-11-18-thermal/`.
