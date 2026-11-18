# incoming/restart - machine restart (patch 010) summary

> Integration note: the integration patch described below is applied to the repository on
> 2026-11-18 and is not kept as a file after the merge; this SUMMARY is the durable record.

**Task:** task-17 (parallel port lanes, agent `driver-restart`). **Date:** 2026-11-18.
**Scope:** implement an upstream-style machine-restart path for the HiSilicon Hi5671Y
"luofu" SoC from the decoded vendor/lab evidence: save the SYSCTRL A/B boot selector,
issue the CRG software-reset command, and fall back to the CRG watchdog if the reset does
not take. Driver + two YAML bindings + Kconfig/Makefile + DTS/config integration, validated
host-side in the `openwrt main` clone. **No device was touched** (no router, no UART, no
TFTP, no flash, no bench tool). Host-only, evidence-graded.

This directory is an **incoming** deliverable: it carries no shared-file edits directly.
Apply `shared.patch` and add the patch file that is already in `patches-6.18/`.

---

## 1. Deliverables

### 1.1 New kernel patch (already in the repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/010-arm-luofu-restart.patch` | `3fbfaaeed7f3038eeb0c56701e4cc52ccdc082e693a04b990336b188c2d25095` |

Numbering: `010` follows the accepted `001`-`009`; the restart port owns no other number.
The patch is a bare unified diff (`--- a/...` / `+++ b/...`, no `diff --git`) in the style
of `004`/`005`/`006`-`009` and applies with `patch -p1` in the OpenWrt platform-patch
order.

`010` adds:

- `drivers/power/reset/luofu-reboot.c` (new): a restart handler over two syscon regmaps.
- `drivers/power/reset/{Kconfig,Makefile}`: `CONFIG_LUOFU_REBOOT` (`bool`,
  `depends on ARCH_LUOFU || COMPILE_TEST`, `select MFD_SYSCON`).
- `Documentation/devicetree/bindings/power/reset/hisilicon,luofu-reboot.yaml` (new): the
  `hisilicon,luofu-reboot` node, with the `hisilicon,sysctrl` and `hisilicon,crg`
  phandle properties.
- `Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml` (new):
  the `hisilicon,luofu-sysctrl`, `"syscon"` SYSCTRL page node that the restart DTS adds
  (TARGET-DESIGN.md 6.2 already planned this binding under `soc/`).

### 1.2 `shared.patch` (apply to the current repository)

| file | sha256 |
| --- | --- |
| `target-skeleton/docs/incoming/restart/shared.patch` | `9ed1ee74a762f8ef90839086f0d82711d2e679933157b37fa6cf8a7cbca7b04d` |

Files and what they carry:

- `target-skeleton/dts/luofu-r116.dts`: adds the `hisilicon,luofu-sysctrl` syscon node
  (0x10100000) and the `hisilicon,luofu-reboot` node (two syscon phandles); updates the
  CRG TODO(restart) text, the DTS header driver list and the "absent nodes" sysctrl
  paragraph to match what now exists (the selector protocol itself stays [U]).
- `target-skeleton/target/linux/hisilicon/luofu/config-6.18`: adds
  `CONFIG_LUOFU_REBOOT=y` as an `[N]` entry, updates the `[N]` legend and the
  MFD_SYSCON/REGMAP provenance comments (the new driver selects MFD_SYSCON, which is
  already in the delta, so no resolved dependency line changes).
- `target-skeleton/README.md`, `docs/SKELETON-SMOKE.md` (new session section),
  `docs/TARGET-DESIGN.md` (3.4, 6.2 and 7.1 rows, TODOs), `docs/TARGET-SKELETON.md`:
  status rows updated to say the restart driver exists as a host-compiled draft, that the
  CRG reset command is PROVISIONAL and that the selector protocol is still [U].

Post-apply hashes (sanity check for the lead):

- `target-skeleton/dts/luofu-r116.dts` = `42c6ffa30b74f1108d2045ae1814e1b3da709752f1e327aabc26b8871752b9b4`
- `target-skeleton/target/linux/hisilicon/luofu/config-6.18` = `25c27473b3bc52ae2d0a207acb4126080578e8c28d59b2656ff0960c9fc7a9c8`

Shared files were **not** edited in the working tree of this session; only the new patch
and this incoming directory are untracked additions.

---

## 2. What the driver does

The SoC has no ARM architectural reset that takes the whole chip down. The restart handler
(registered with `devm_register_restart_handler()`, default priority) runs three steps:

1. **Save the boot selector.** Read SYSCTRL `0x10100000 + 0x0c08`, check that its low byte
   is the measured A-pair (`0x10`) or B-pair (`0x11`) encoding, and write the same value
   back (a write-same). Any other value - including the observed `0x21` handover - is left
   untouched with a warning.
2. **Request the CRG software reset.** Write the vendor `softrst_val0` then
   `softrst_val1` pair (the second is the bitwise complement of the first) to the CRG
   reboot register at page offset `0x00`. **PROVISIONAL** (see section 3).
3. **Watchdog fallback.** If step 2 does not reset the SoC, set the CRG watchdog timeout to
   1 s (hwver-2 encoding, field `[18:10]` of the set word) and write the vendor enable
   pair val1-then-val0 to the enable word, then wait 1.5 s for the hardware reset. If the
   watchdog reset also does not happen, the handler returns `NOTIFY_DONE` and the ARM
   `machine_restart()` path prints its usual "Reboot failed -- System halted".

Probe obtains the two regmaps and registers the handler; it performs **no register access
at all**. The driver never calls `ioremap()`, `request_mem_region()` or
`platform_get_resource()`; both pages stay owned by their syscon nodes, the same design the
CRG/watchdog patches use.

---

## 3. Design decisions (and why)

1. **Standalone driver in `drivers/power/reset/`, not a machine hook.** Upstream keeps SoC
   restart implementations under `drivers/power/reset/` as platform drivers registering a
   restart handler; a `.restart` hook in a mach file would need early statics and would
   duplicate what the syscon nodes already provide. This also matches the target design's
   plan for a future reset driver (TARGET-DESIGN.md 7.1).
2. **Two syscon regmaps, no MMIO claim.** SYSCTRL (0x10100000) and CRG (0x14880000) are
   pages with several owners. The two new YAMLs model the SYSCTRL page as
   `hisilicon,luofu-sysctrl, "syscon"` and reference the already-written
   `hisilicon,luofu-crg, "syscon", "simple-mfd"` node; neither page is mapped here. The
   lab lane's failure mode (a second claim / `devm_ioremap()` workaround) cannot recur.
3. **The selector write is a write-same, not the lab's `0x10`.** The lab machine wrote
   `0x10` unconditionally before a reset because its failover logic had flipped the cell
   and it needed to land on stock A; that is a bench policy, not upstream semantics. The
   measured facts are: the cell is sticky across a warm reset, the loader decodes the low
   byte, and `0x10`/`0x11` are the A/B encodings. Writing back the value just read
   preserves the boot pair under any reset path and cannot move the board to another slot,
   so it is the only value this driver can justify. An unrecognised value is left alone.
4. **The CRG command pair is PROVISIONAL, and that is stated in code, binding and DTS.**
   The values and the register offset are the vendor devicetree's own
   (`crg@14880000`: `reboot-offset = <0x0>`, `softrst_val0/1 = <0x51162100>/<0xaee9deff>`);
   the recovered CRG access rules name `0x00` the reboot register and list it as a store
   the read-only lab instruments must not touch - i.e. writing it is expected to reset the
   SoC. What is **not** measured is the exact sequence: whether the pair is written to the
   same register (this driver's choice, matching the vendor watchdog pair shape) or to two
   adjacent words, and in which order. The driver therefore confines the write to the
   reboot register (never soft-reset `0x84`, resume `0x38`, mux `0x138`, PLL
   `0x198`/`0x1e0`) and bounds a command that does not take with the watchdog fallback.
   No magic value is invented: both values are the vendor DT's.
5. **The watchdog fallback is the lab-proven stranding fix, not a new idea.** PHASE 85 in
   the parent ledger records the failure exactly: the restart handler wrote the reset
   sequence, parked forever, and the SoC watchdog had already been disarmed - the board
   could only be recovered by hand. The fix was to arm the watchdog as a hardware reset
   source before parking. This driver uses the same values the `005` watchdog driver
   carries (vendor DT hwver-2 data, enable val0/val1) and the same 1 s expiry, and never
   parks: it waits a bounded time and returns if the watchdog did not fire.
6. **Why not reuse/extend the watchdog driver or `POWER_RESET_HISI`:**
   - the watchdog driver is a watchdog API driver and `005` deliberately refuses a restart
     handler (reset path unverified, and a restart handler there could arm the timer);
   - `POWER_RESET_HISI` (already `=y` from the generic config) binds
     `"hisilicon,sysctrl"` only and writes `0xdeadbeef` at a `reboot-offset` property;
     this SoC's vendor node is `hsan,sysctrl` with no such property, and making the new
     SYSCTRL node match `hisilicon,sysctrl` would give that driver an MMIO claim next to
     the syscon one. The new SYSCTRL binding name keeps the two apart;
   - `syscon-reboot` (CONFIG_POWER_RESET_SYSCON, not set in the target config) writes one
     register and has no way to do the selector save + second stage.
   The watchdog enable values therefore appear twice (in `005` and here). That is a known
   review smell; the alternative - exporting watchdog internals or linking two drivers - is
   worse. If review prefers, the constants can move to a small shared header later.
7. **Probe discipline.** Like `004`/`005`, probe touches no register; the first MMIO
   access happens when userspace asks for a reboot. Loading the driver cannot arm the
   watchdog, write the selector or reset anything.

---

## 4. Evidence grading

| claim | source in the repository / ledger | grade |
| --- | --- | --- |
| Selector cell is SYSCTRL `0x10100C08`; low byte `0x10` = A pair, `0x11` = B pair | vendor DT `hsan,sysenv bootreg-offset = <0xc08>` (`opensource/docs/soc/luofu-r116-pinned.dts:1417-1422`); U-Boot decompile (parent ledger 2026-10-11/2026-11-17 entries); live devmem A/B test (parent ledger 14926-14932) | [V] |
| The cell is sticky across a warm reset and the loader re-stamps the env `bootflag` | parent ledger 14931-14932; parent `BENCH-FACTS.md` §5 (via TARGET-DESIGN.md 3.4) | [V] |
| `0x00` (empty) makes the loader validate and record a value; `0x21` was written by the vendor sysenv interface as a handover | parent ledger 14913-14917, 22623; vendor DT sysenv node | [V] for the values, [U] for the meaning of `0x21` |
| The lab machine wrote `0x10` to the selector before its reset attempts | `tools/lab-src/arch/arm/mach-luofu/luofu.c:154-172`; parent ledger PHASE 85 | [V] |
| The stock restart path saves the selector before resetting; the value it writes is not measured | parent task brief / parent evidence; **not** independently re-measured in this session | [I]/[U] |
| CRG reboot register is page offset `0x00`; the vendor node carries `reboot-offset = <0x0>` | vendor DT `crg@14880000` (`luofu-r116-pinned.dts:467-482`); recovered access rules list "reboot `0x00`" (`opensource/lab/luofu-clk/bindings/hisilicon,luofu-crg.md:105-113`) | [V] offset, [I] naming |
| `softrst_val0 = 0x51162100`, `softrst_val1 = 0xaee9deff` are the vendor's reboot/soft-reset pair | vendor DT `crg@14880000:479-480`; `opensource/lab/luofu-clk/luofu-clk.c:100-102,507-517` | [V] values, [U] exact use |
| The pair is bitwise complementary (`val1 == ~val0`) | direct arithmetic on the two vendor values | [V] |
| CRG watchdog set/enable words and hwver-2 magic values (`0x50`/`0x64`, `0x4f4e5452`/`0x12345116`) | vendor DT `watchdog` node (`luofu-r116-pinned.dts:1512-1531`); watchdog patch `005` decode | [V] values |
| Watchdog 1 s expiry resets the SoC | lab kernel force-reset path and PHASE 85 ledger; the watchdog itself has been observed resetting the board (30 s U-Boot-armed timeout) | [V] for the watchdog class, [I] for this exact arming path |
| A restart attempt that does not take must not park with the watchdog disarmed | parent ledger PHASE 85 (2026-10-12) | [V] |
| Writing the CRG reboot register resets this SoC | **not measured**; only inferred from the vendor DT + the access rules | [U] |

---

## 5. Validation (exact commands and results)

### 5.1 Environment

- Validation clone: an isolated btrfs-reflink copy of the shared clone,
  `/home/shibbityshwab/openwrt-lab-restart` (`openwrt main` `27663a0`, kernel
  `linux-6.18.60`, the tree's own OpenWrt GCC 14.4.0 toolchain). The shared
  `/home/shibbityshwab/openwrt-lab` clone was **not** modified by this lane.
- The clone was given the complete target patch series `001`-`010` (the six earlier port
  lanes plus this one), the delivered DTS and the delivered `config-6.18`, so the build
  below is the integrated series, not this driver in isolation.
- Pristine tree for the patch dry-run: `a/` extracted from the clone's
  `dl/linux-6.18.60.tar.xz` (`VERSION = 6`, `PATCHLEVEL = 18`).
- Logs and intermediate artifacts are local-only (gitignored) under
  `build/linux-kbuild/waves/upstream-6.18-restart-20261118/`.

### 5.2 Pristine-6.18.60 patch dry-run - PASS

```
$ cd .../upstream-6.18-restart-20261118/a            # pristine tree
$ patch -p1 --dry-run -i 010-arm-luofu-restart.patch
checking file drivers/power/reset/Kconfig
checking file drivers/power/reset/Makefile
checking file drivers/power/reset/luofu-reboot.c
checking file Documentation/devicetree/bindings/power/reset/hisilicon,luofu-reboot.yaml
checking file Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml
RC=0
```

The same command applied for real in a throwaway copy reproduces the three new files
byte-for-byte (`cmp` PASS), so the patch carries exactly the delivered sources.

### 5.3 OpenWrt prepare - RC=0

```
$ cd /home/shibbityshwab/openwrt-lab-restart && make target/linux/prepare V=s
...
Applying .../patches-6.18/010-arm-luofu-restart.patch using plaintext:
patching file drivers/power/reset/Kconfig
...
RC=0
```

The prepare log contains 1,169 `patching file` lines over the OpenWrt generic backports and
the target series; no `.rej`/`.orig` file exists in the target or the kernel tree, and the
kernel tree contains `drivers/power/reset/luofu-reboot.c` with both YAMLs.

### 5.4 Full kernel compile - RC=0, zero warnings

```
$ make target/linux/compile V=s -j16
  CC      drivers/power/reset/luofu-reboot.o
...
  Kernel: arch/arm/boot/zImage is ready
time: target/linux/compile#1258.56#77.36#111.15   (time.pl: child user CPU#, child sys CPU#, wall seconds)
RC=0
```

- `arch/arm/boot/zImage`: **3,251,336 B**, sha256 `e548bc7b4c54160da2b482fcb2111d5569227c1132ba4c0299ba2e665d26aeb4`.
- `drivers/power/reset/luofu-reboot.o`: 18,216 B.
- kernel `.config`: `CONFIG_LUOFU_REBOOT=y` plus `CONFIG_POWER_RESET=y`,
  `CONFIG_POWER_RESET_HISI=y` (pre-existing), `CONFIG_MFD_SYSCON=y`, `CONFIG_REGMAP=y`
  and `CONFIG_WERROR=y` (the tree's target config).
- the driver is built-in and linked: `System.map` has `luofu_reboot_probe` (`c0ba94a8`)
  and `luofu_restart_handler` (`c07082cc`).
- **0 warning lines and 0 error lines** in the whole compile log; the only 38 "warning"
  strings are the pre-existing `modpost: missing MODULE_DESCRIPTION()` notices for
  unrelated generic modules (netfilter, phylink, ppp, ...), none touching luofu.
- load-address contract unchanged: both `.head.o.cmd` files carry
  `-DTEXT_OFFSET=0x00608000`; `System.map` gives `_text = stext = 0xc0608000` and
  `swapper_pg_dir = 0xc0604000`, i.e. load/entry `(0x83200000 & 0xf8000000) + 0x00608000 =
  0x80608000` - the image recipe is untouched.

### 5.5 Board DTS -> DTB with the patched kernel's own dtc - RC=0, zero warnings

```
$ cpp -nostdinc -I <kernel>/include -I <kernel>/arch/arm/boot/dts -undef \
      -D__DTS__ -x assembler-with-cpp -o luofu-r116.dts.preprocessed dts/luofu-r116.dts
$ <kernel>/scripts/dtc/dtc -I dts -O dtb -o luofu-r116.dtb luofu-r116.dts.preprocessed
RC=0, stderr EMPTY (zero warnings)
```

- DTB: **9,891 B**, sha256 `f23fc9319c3f0d9fc3cad6aa0a6578fde9a5f4f182ba27a5ff00f5cea44383cb`.
- node checks (`fdtget`): `/reboot` compatible `"hisilicon,luofu-reboot"`;
  `/system-controller@10100000` compatible `"hisilicon,luofu-sysctrl", "syscon"` and
  `reg = <0x10100000 0x1000>`.
- The real board DTB was also run through `dt-validate` against the processed schema:
  RC=0, and **no message names the reboot or SYSCTRL node** (the remaining messages are the
  pre-existing TODO nodes: sp804 compatible length, UART clock-names, thermal zone naming).
  A full-draft-DTS `dtbs_check` is still not clean by design and is not claimed.

### 5.6 `dt_binding_check` for both new YAMLs - RC=0

```
$ PATH=<dtschema-venv>/bin:$PATH make ARCH=arm dt_binding_check \
      DT_SCHEMA_FILES="Documentation/devicetree/bindings/power/reset/hisilicon,luofu-reboot.yaml:Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml"
  DTEX    .../hisilicon,luofu-reboot.example.dts
  DTC [C] .../hisilicon,luofu-reboot.example.dtb
  DTEX    .../hisilicon,luofu-sysctrl.example.dts
  DTC [C] .../hisilicon,luofu-sysctrl.example.dtb
RC=0
```

dtschema **2026.9**; `yamllint` is not installed, so the tree's YAML-lint step is skipped
with its own warning - the only validation step not exercised (same limitation as the
earlier sessions).

### 5.7 Static checks - clean

```
$ scripts/checkpatch.pl --no-tree -f drivers/power/reset/luofu-reboot.c
  total: 0 errors, 0 warnings, 258 lines checked
$ scripts/checkpatch.pl --no-tree -f Documentation/devicetree/bindings/power/reset/hisilicon,luofu-reboot.yaml
  total: 0 errors, 0 warnings, 69 lines checked
$ scripts/checkpatch.pl --no-tree -f Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml
  total: 0 errors, 0 warnings, 44 lines checked
```

- no `ioremap()`/`request_mem_region()`/`platform_get_resource()` call in the driver (the
  only text match is the comment saying they are absent); both pages are obtained with
  `syscon_regmap_lookup_by_phandle()`;
- the probe body contains no `regmap_read`/`regmap_write` call: loading the driver cannot
  touch a register;
- exactly six `regmap_write` call sites exist and all are in the restart handler / its
  helpers (selector save-same, two CRG command writes, watchdog set word, two enable
  words);
- no lab/bench breadcrumb, vendor `hsan,*` string, `0xc0de*` cell or private-repository
  reference in the driver or the two YAMLs (`grep -i 'lab|bench'` -> no matches).

### 5.8 Config provenance - canonically re-derived, symbol-identical

With the new Kconfig in the tree, the canonical top-level

```
$ yes '' | make kernel_oldconfig
RC=0
```

emitted 297 config-symbol lines, **identical** to the annotated `luofu/config-6.18` after
stripping comments (`CONFIG_SYMBOLS_IDENTICAL`; only line order differs), including
`CONFIG_LUOFU_REBOOT=y`; a second run was byte-identical (`IDEMPOTENT_BYTE_IDENTICAL`) and
the clone's target config was restored afterwards. The new driver's `select MFD_SYSCON`
adds no resolved dependency line because MFD_SYSCON was already in the delta.


---

## 6. Proven vs unverified (evidence grading)

**Proven, host-only (this session):** the patch applies cleanly to pristine `linux-6.18.60`
and in the OpenWrt clone's platform-patch order; the full 6.18 kernel compiles as a built-in
with `CONFIG_WERROR=y` with zero warnings; both YAMLs pass `dt_binding_check`; the board
DTS compiles with the patched kernel's own dtc with zero warnings and the new nodes validate
against their schemas; `checkpatch` is clean; probe performs no register access; no vendor
or lab instrumentation is embedded.

**Unverified (no hardware run; do not read this port as a working reboot path):**

- whether writing the vendor pair to CRG `0x00` resets the SoC at all (PROVISIONAL);
  the write order and whether the pair belongs in one register or two adjacent ones;
- whether the watchdog enable sequence from our own kernel takes (the values are the
  vendor driver's, transcribed; the effect has been measured only through the bootloader's
  own 30 s arming and the lab force-reset lane);
- the exact semantics of the `0x21` selector value (the driver leaves it untouched);
- the selector protocol as a whole: the read path a `sysupgrade` helper needs and one
  attended A/B validation are still open (TARGET-DESIGN.md 3.4);
- no boot, no bench, no device, no image. **Publication remains NOT READY.**

---

## 7. Bench-validation plan / remaining blockers

A single attended bench cycle answers this port's open questions; it does not need the
upstream submission. Order of measurement (each step is one reboot, the box has UART
read-only and the recovery door must be armed):

1. **Watchdog fallback alone.** Boot an image with the CRG command disabled (a temporary
   local build) and `reboot`; confirm the box comes back and the watchdog was the reset
   source (boot_id changed, U-Boot banner shows the watchdog reset). This measures the
   fallback without relying on the unproven command.
2. **CRG command.** Boot the delivered configuration and `reboot`; if the box returns
   without reaching the fallback's "arming the watchdog" line, the pair/offset/order are
   right. Try the three plausible encodings (val0,val1 to one register; val1,val0; val0 at
   `0x00` + val1 at `0x04`) only as separate measured runs, one per build - never as a
   combined shotgun.
3. **Selector save.** After a successful reset, read the cell (vendor sysfs or a read-only
   probe) and confirm it still holds the pair that was booted; then one A/B validation as
   described in TARGET-DESIGN.md 3.4 to close the protocol question for `sysupgrade`.

Upstream review items: split the binding YAMLs into separate patches (checkpatch's
`DT binding docs and includes should be a separate patch` - the series style keeps them
with the driver, like `004`/`005`); the maintainer placeholder identity; whether
`hisilicon,sysctrl`/`hisilicon,crg` phandle names are the right property names (an
`nvmem` cell provider for the selector might be preferred upstream); whether the
watchdog constants should be shared with the watchdog driver; and the priority choice
(default 128 through `devm_register_restart_handler`).

---

## 8. Notes for the lead (apply/commit)

- Apply: `cd opensource && git apply target-skeleton/docs/incoming/restart/shared.patch`
  (verified `git apply --check` RC=0), then commit the new patch, this `incoming/restart/`
  directory and the shared-file changes together. Do not apply the patch twice.
- Per the `port-notes/` convention this SUMMARY can be moved to
  `target-skeleton/docs/port-notes/restart/SUMMARY.md` (with the integration note already
  at the top) and the `incoming/` directory removed after the merge; the net and FMC lanes
  were integrated that way.
- Conflict risk: `shared.patch` touches `target-skeleton/dts/luofu-r116.dts`,
  `config-6.18` and the four docs files that other incoming lanes may also edit. No kernel
  source overlaps with any other lane (`010` is the only file in
  `drivers/power/reset/`). If another lane landed first, use `git apply -3` and union the
  doc hunks.
- The validation clone was an isolated reflink copy
  (`/home/shibbityshwab/openwrt-lab-restart`); the shared `openwrt-lab` clone was not
  modified by this lane.
