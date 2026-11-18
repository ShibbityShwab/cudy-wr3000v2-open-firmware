# SMP-RE lane - integration record (task-32, agent smp-re)

Date: 2026-11-18. Host-only. **No device, router, UART, TFTP, flash or bench tool
was touched; nothing was booted; no SMP claim is made.**

This directory is the incoming package for the SMP reverse-engineering lane. It holds
the shared-file integration patch; the kernel-side implementation lives in
`patches-6.18/018-arm-mach-hisilicon-smp.patch`, the evidence/design document is
[`docs/SMP-RE.md`](../../../../docs/SMP-RE.md), and the local-only evidence bundle is
`build/linux-kbuild/waves/upstream-6.18-smp-20261118/` (gitignored).

## 1. What was delivered

| artifact | path | role |
| --- | --- | --- |
| SMP reverse-engineering + design doc | `docs/SMP-RE.md` (539 lines) | evidence-graded decode of the vendor `hisilicon,hsan_smp` release, platsmp/CPU_METHOD_OF_DECLARE design, dependencies, risks, staged plan, effort, [U] list, validation record |
| draft kernel patch | `target-skeleton/target/linux/hisilicon/patches-6.18/018-arm-mach-hisilicon-smp.patch` | new `arch/arm/mach-hisilicon/platsmp.c` + machine Kconfig/Makefile/descriptor refactor; sha256 `5509ca597852143a22040d8421cd794397f8f85fee841d611de68e53cca5e00b` |
| incoming shared patch | `docs/incoming/smp/shared.patch` (this directory) | DTS `enable-method` + TARGET-DESIGN/README/SKELETON-SMOKE updates; sha256 `081c59879a546d30f6ad4000d387aa706da08a87d397ea1b1a173a580183bfd4` |
| this record | `docs/incoming/smp/SUMMARY.md` | integration notes, validation results, gates |
| local evidence bundle | `build/linux-kbuild/waves/upstream-6.18-smp-20261118/` | vendor vmlinux extracts, recovered kallsyms tables, vendor base DTB, symbol/function dumps, validation logs and hashes |

## 2. What the decode established (summary; full detail in SMP-RE.md)

The stock vendor kernel (5.10.201) releases the second Cortex-A9 through an
`early_initcall` plus a single `smp_boot_secondary` operation registered as the
devicetree CPU method `"hisilicon,hsan_smp"`:

1. the early init maps SYSCTRL (0x10100000) and CRG (0x14880000) and reads the vendor
   DT values: pen word at `SYSCTRL + smp-offset (0xc00)`, resume word at
   `CRG + resume-offset (0x38)`, per-core resume bits from
   `core-info = <0x16 0x18 0x17 0x19>`;
2. `hsan_boot_secondary(cpu)` writes `__pa_symbol(secondary_startup)` to the pen
   word, read-modify-writes the resume word clearing the target core's two bits
   (CPU0: 0x16/0x18, CPU1: 0x17/0x19), then sends the ARM wakeup IPI.

There is **no SMC, no PSCI and no secure-monitor call in the Linux-side release path**
(the repository's earlier "pen/smc" phrasing is corrected: pen [V], SMC [X]). The
design is identical in the 2.4.15 and 2.5.24 stock kernels (decoded from both vmlinux
images; same 76,769 symbols, instruction-identical release body) and a stock boot
capture shows 2 CPUs. Evidence: `docs/SMP-RE.md` sections 2-4 and the local bundle
`01-RE-evidence.md`.

## 3. What patch 018 implements

* new `arch/arm/mach-hisilicon/platsmp.c`:
  - `CPU_METHOD_OF_DECLARE(luofu_smp, "hisilicon,luofu-smp", &luofu_smp_ops)` - the
    documented method name, deliberately not the vendor's private `"hisilicon,hsan_smp"`;
  - `smp_prepare_cpus`: enables the Cortex-A9 SCU (mainline mach-hisi idiom; the vendor
    method never does this) and ioremaps the existing `hisilicon,luofu-sysctrl` /
    `hisilicon,luofu-crg` syscon nodes;
  - `smp_boot_secondary`: the decoded pen + resume-bit + IPI release, with
    **fail-safe** behaviour (missing nodes => `-ENODEV`, no writes; unknown CPU =>
    `-EINVAL`);
  - pen offset 0xc00, resume offset 0x38 and the resume bit numbers are driver data
    with vendor-property citations - no new DT binding surface, no new YAML;
* machine refactor: `arch/arm/mach-hisilicon/Makefile` (`obj-$(CONFIG_SMP) += platsmp.o`),
  Kconfig help text and the `luofu.c` machine-descriptor comment now state the DT
  enable-method design instead of the former "UP-only / no smp_ops" TODO.

No config change is needed: the target kernel config already resolves
`CONFIG_SMP=y`, `CONFIG_HAVE_ARM_SCU=y` and `CONFIG_HAVE_ARM_TWD=y` (patch 001's
ARCH_LUOFU selects), and the re-derivation is byte-idempotent (section 5).

## 4. What shared.patch changes (apply from the opensource/ root)

    cd opensource
    git apply target-skeleton/docs/incoming/smp/shared.patch

Files and base state (base = `opensource` HEAD `026cbe5`,
`docs: MISC-SOC-BLOCKS - MAC source resolved ...`; the patch applies with
`git apply` and `patch -p1` from the `opensource/` root):

| file | change |
| --- | --- |
| `target-skeleton/dts/luofu-r116.dts` | `/cpus/enable-method = "hisilicon,luofu-smp"` replacing the SM TODO (this is the single line that turns the release on; the driver is fail-safe without it) |
| `docs/TARGET-DESIGN.md` | section 6.2 SMP row and section 7.1 machine/SMP rows updated to "written as patch 018"; section 8.5 gains the SMP status/risks/staging paragraph; open question 12.8 rewritten (decoded + UP is acceptable for a first submission) |
| `target-skeleton/README.md` | new status paragraph for the SMP-RE session (above the PUBLICATION note) |
| `docs/SKELETON-SMOKE.md` | new "2026-11-18 (SMP-RE session)" validation section |

Validation of the patch itself: `git apply --check` RC=0 and `git apply` RC=0 on a
fresh `git archive` of the opensource HEAD, with the four edited files byte-identical
to the delivered copies; `patch -p1 --dry-run` also RC=0.

**Staging note (important for the lead):** the DTS hunk is the attended-bring-up
switch. For an *UP-first mergeable* submission (SMP-RE.md stage S1) drop just that
DTS/hunk; patch 018 stays inert without it (no matching CPU method => UP boot), and for
an upstream submission the DT should then carry a single `cpu@0` node (two DT CPUs
with no matching method is exactly the 5.10 bench failure mode documented in SMP-RE.md
risk 6). The current tree therefore describes the **S2 attended-bring-up end state**,
not an accepted SMP claim.

## 5. Validation (host-only, in an isolated clone)

Isolated btrfs-reflink copy of the shared clone (`openwrt main` `27663a0`, `linux-6.18.60`, tree's own OpenWrt GCC 14.4.0). The
shared clone was not modified. All logs are in the local evidence bundle.

| check | result |
| --- | --- |
| pristine-series apply | fresh `linux-6.18.60`: patches 001-018 in order with `patch -p1` RC=0, no `.rej`/`.orig`; only pre-existing offset adjustments in patch 008 (Kconfig/Makefile hunks; expected - generated against the OpenWrt-prepared tree); final mach-hisilicon files byte-identical to the validated series tree |
| patch 018 dry-run/apply | on a pristine+001-017 tree: `patch -p1 --dry-run` RC=0, real apply RC=0, byte-identical; `git apply --check` RC=0 |
| `make target/linux/prepare` | RC=0 (series 001-018; 1,202 "patching file" lines; no `.rej`/`.orig`) |
| `make target/linux/compile` | **RC=0**, 20m06s, zImage 3,264,472 B sha256 `ae59d99862446751cc5389a548c9692e9a02fbcde64fedaae42ff831839eda41`; **0 warnings attributable to new/changed files** (the 38 modpost `missing MODULE_DESCRIPTION` warnings are the identical pre-existing baseline of the 001-017 and restart lanes) |
| platsmp built | `arch/arm/mach-hisilicon/platsmp.o`; System.map: `luofu_smp_boot_secondary`, `luofu_smp_prepare_cpus`, `luofu_smp_ops`, `__cpu_method_of_table_luofu_smp`, `luofu_smp_pen`, `luofu_smp_resume` |
| TEXT_OFFSET unchanged | `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000`; `_text = stext = 0xc0608000` |
| config | no patch-introduced symbols; `CONFIG_SMP=y`/`HAVE_ARM_SCU=y`/`HAVE_ARM_TWD=y` already resolved; `make kernel_oldconfig` three runs RC=0, kernel `.config` byte-identical to the 001-017 build (sha256 `1b9b084e...`), re-derived target config fragment same 300-symbol set and byte-idempotent (sha256 `4106877f...`) |
| dt_binding_check | RC=0 for all 13 series YAMLs (dtschema 2026.9; yamllint unavailable - one lint step skipped, same caveat as earlier lanes) |
| board DTS | repo DTS with the enable-method hunk: cpp RC=0; patched-kernel dtc RC=0 with **zero warnings**; DTB 10,710 B sha256 `bc599a4e0bf5363770df4372a0c88ffab9097ea2127986631842b486ef337748` |
| dt-validate | RC=0; only the same six pre-existing findings as the 001-017 lane (arm,sp804 compatible too short; UART clock-names); none names the SMP nodes or the DTS change |
| checkpatch | `platsmp.c` 0 errors/0 warnings; Kconfig, Makefile, luofu.c 0/0; the bare patch file only reports the expected missing commit-description/Signed-off-by metadata of the working patch format (the review-form copy under `opensource/upstream/` is not updated by this lane) |
| shared.patch | `git apply --check` RC=0, `git apply` RC=0, `patch -p1 --dry-run` RC=0; applied files byte-identical to the delivered copies |

## 6. What is NOT claimed / open

* **No hardware behaviour and no SMP claim.** The release has never run on our kernel;
  the attended bring-up acceptance gates, failure modes and the [U] list are in
  `docs/SMP-RE.md` sections 7-8 and 10 (pen-word consumer, CRG resume-bit semantics,
  SCU pre-state, LPT/TWD/GIC, secure-world traps, TEXT_OFFSET pen value).
* The upstream submission series (`opensource/upstream/series-6.18/`) is **not**
  updated (still 001-017); patch 018 is not yet in review form.
* No vendor binaries/blobs are introduced into the repository: the evidence bundle is
  local-only and gitignored; shared.patch adds text only.
* No config change was needed (see section 3); if a future attended step needs
  `CONFIG_HOTPLUG_CPU`, that is a separate design (the vendor table has no hotplug
  operations and the vendor kernel was built with hotplug off).
