# A/B install path - host-only session summary (2026-11-18)

**Status: the A/B boot-selector interface (patch `017`) and the sysupgrade
write path (`platform_do_upgrade`) are implemented and validated host-side.
NOTHING HAS BOOTED OR BEEN FLASHED.** The sysupgrade container still says
NOT FLASHABLE (it has never booted), the loader's env re-stamp is unmeasured,
and one attended A/B validation is still owed before the install path may be
called hardware-validated. This file records the design, the evidence, the
exact selector values and the commands/results of the A/B install session
(shared task-27, agent `ab-install`). Nothing here is a boot, flash or install
claim.

This directory is the hand-off: `shared.patch` (apply from the `opensource/`
root) contains the README, SKELETON-SMOKE, TARGET-DESIGN, TARGET-SKELETON,
port-notes, board-DTS, config and target-Makefile updates. Directly edited
repo files (not in this record's patch): patch `017`, the target's
`base-files/lib/upgrade/platform.sh` and the new
`tools/test-platform-abinstall.sh`.

## 1. Selector semantics used (all measured; nothing invented)

The stock loader selects the complete firmware pair from a 32-bit SYSCTRL cell
(`0x10100000 + 0xc08`). The low byte is what the loader interprets:

| value | meaning | evidence |
| --- | --- | --- |
| `0x10` | A pair = `kernela` (mtd11) + `rootfsa` (mtd13) | loader's own "bootreg init, set 10"; controlled write -> reboot -> booted A pair with the running rootfs read back. Parent `BENCH-FACTS.md` section 5; ledger phase 67. |
| `0x11` | B pair = `kernelb` (mtd12) + `rootfsb` (mtd14) | loader's "set 11"; controlled write -> reboot -> booted B pair. Same sources. |
| `0x21` | vendor "sysenv" manual/recovery B handover (the vendor slot-switch recipe writes it) | measured as a write that boots B (`FLASH-PLAN.md` "Slot switch recipe"; `BENCH-FACTS.md` section 5). Its post-boot cell state has never been measured -> the kernel reports it as `manual-b` and the automated installer refuses it. |
| `0x00` | un-initialised at a true power-on; the loader validates and records a pair | `BENCH-FACTS.md` section 5. Reported as `uninitialised`; the automated installer refuses it. |

- **The env blocks are not a second selector.** `enva`/`envb` (mtd3/mtd4,
  format `crc32-LE(4) | flags(1) | data(131067)`) are re-stamped by the loader
  from the cell; the controlled tests found the env `bootflag` to be an
  output, and the earlier "plain `fw_setenv` works" path is a known trap.
  The install path therefore writes the cell and **not** the env blocks; one
  attended A/B validation has to confirm the re-stamp.
- The cell is shared with the machine-restart driver (patch `010`, which
  write-sames it before a reset). Patch `017` talks to the same SYSCTRL
  regmap and never maps or claims the page, so there is no second ioremap and
  no ownership fight.

## 2. What was delivered

| file | what it is |
| --- | --- |
| `patches-6.18/017-soc-luofu-boot-selector.patch` | new `drivers/soc/hisilicon/luofu-selector.c`, Kconfig `LUOFU_SELECTOR` + Makefile, YAML binding `soc/hisilicon/hisilicon,luofu-boot-selector.yaml`, ABI doc `sysfs-driver-luofu-selector`. Probe performs **no register access**; `current_pair` decodes the cell, `next_pair` stores only `a` (0x10) / `b` (0x11), rejects anything else with `-EINVAL` and fails with `-EIO` if the read-back does not stick. |
| board DTS (in `shared.patch`) | enabled `boot-selector` node: `compatible = "hisilicon,luofu-boot-selector"`, `hisilicon,sysctrl = <&sysctrl>`, `hisilicon,selector-offset = <0x0c08>`. |
| config (in `shared.patch`) | `CONFIG_LUOFU_SELECTOR=y` in `luofu/config-6.18`; `ubi-utils` added to `DEFAULT_PACKAGES` (the write path calls `ubiformat`, which `switch_to_ramfs()` copies only when installed). |
| `base-files/lib/upgrade/platform.sh` | `platform_check_image` unchanged in scope; `platform_do_upgrade` implemented (below). |
| `tools/test-platform-abinstall.sh` | host-side mocked harness for the write path (below). |

## 3. Install sequence implemented in `platform_do_upgrade`

1. Re-run `platform_check_image` (defence in depth; a direct `do_stage2`
   invocation must not write an unvalidated container).
2. Resolve the **active pair** from two independent in-boot witnesses that
   must agree: the `luofu-selector` `current_pair` (the cell the loader reads
   on the next boot) and the rootfs slot this system booted from
   (`/sys/class/ubi/ubi0/mtd_num` cross-checked with that mtd's `/proc/mtd`
   label - the same witness the bench slot gate used). `/proc/cmdline` is not
   used: the measured boot carries no slot token. Unmeasured selector states,
   a missing interface or a selector/rootfs disagreement refuse before any
   write.
3. Resolve the **inactive pair** by `/proc/mtd` label only:
   `kernelb`+`rootfsb` after an A boot, `kernela`+`rootfsa` after a B boot.
   Verify the measured sizes (kernel slot 8,650,752 B; rootfs partition
   24,379,392 B, from `dumps/router-20261108/full/proc_mtd.txt`) and that the
   member fits the image budgets (kernel <= 8,650,752 B, rootfs <=
   24,313,856 B, the recipe's UBI budget).
4. Extract only the `kernel` and `root` members; re-check the uImage and
   UBI# magics.
5. Write the kernel slot first (`mtd write`: erase+write), then read back the
   exact member length and compare md5 with the member. Then `ubiformat` the
   rootfs slot and re-read PEB 0's `UBI#`/`UBI!` headers.
6. `sync`, and only then move the selector to the inactive pair, reading it
   back. If that store or its read-back fails, restore the active pair. On any
   earlier failure the selector is untouched, so the next boot still comes up
   on the untouched pair.
7. Return; `/lib/upgrade/do_stage2` runs its existing `reboot -f`. This
   function never reboots, never erases `rootfs_data` (mtd15 is a separate
   shared partition, so the overlay is preserved), never touches the env
   blocks and never writes the active pair.

## 4. Selector driver ABI (patch 017)

```
/sys/devices/platform/luofu-selector/current_pair   RO  "a" | "b" | "manual-b" | "uninitialised" | "unknown"
/sys/devices/platform/luofu-selector/raw            RO  0x........ (whole 32-bit cell, diagnostics)
/sys/devices/platform/luofu-selector/next_pair      WO  "a" -> 0x10, "b" -> 0x11; -EINVAL otherwise; -EIO if read-back differs
```

The store writes the full dword (the measured `devmem 0x10100C08 32 0x10/0x11`
operation), warns but proceeds when the current cell is outside the measured
encodings (so a damaged cell can be repaired from userspace), and verifies the
read-back. The manual/recovery value `0x21` is deliberately not reachable
from this interface.

## 5. Mocked host-side tests

`tools/test-platform-abinstall.sh` sources the real `platform.sh`, exports
its `LUOFU_TEST_ROOT` hook to a fixture tree and calls `platform_do_upgrade`
against fake `/proc/mtd` (stock 17-slot map), fake
`/sys/class/ubi/ubi0/mtd_num`, fake selector sysfs and
`mtd`/`ubiformat`/`sync`/`reboot` shims that log every call. It covers:

- A boot -> writes `kernelb` then `rootfsb`, selector `b`, kernel-before-rootfs
  before first sync order, no access to mtd11/mtd13/mtd3/mtd4/mtd15;
- B boot -> writes `kernela` then `rootfsa`, selector `a`;
- refusals with no write: selector `0x21` manual, unknown selector, missing
  selector, selector/rootfs pair mismatch, kernel member over the slot, rootfs
  member over the budget, foreign image (real `platform_check_image`), failed
  container re-check;
- failing kernel write -> no `ubiformat`, selector stays `a`;
- failing rootfs format -> selector stays `a`;
- failing selector store -> selector stays `a`;
- `--container` mode: the real built `sysupgrade.bin` through the real
  `platform_check_image` and the write path.

Results: 52/52 assertions pass against the synthetic fixture containers; with
  `--container` the same suite runs the real built sysupgrade.bin
  (6,185,262 B, sha256 `0303d76a...`) through the real
  `platform_check_image` and all 58 assertions pass.

## 6. Validation in the isolated clone

Clone `/home/shibbityshwab/openwrt-lab-abinstall` (copy of the shared
`openwrt-lab`; the shared tree was not modified). The target-skeleton target
plus patch `017` and the shared edits were installed, the kernel build dir was
deleted and re-created by `make target/linux/prepare`, then
`make target/linux/compile V=s -j16`:

- OpenWrt main commit `27663a0`, `linux-6.18.60`; the target was installed
  from the repository skeleton with patch `017` and the shared edits, the
  kernel build dir deleted and re-created by `prepare`.
- `patch -p1 --dry-run` of `017` against pristine `linux-6.18.60`: RC=0;
  serial apply `001`-`017`: RC=0, 0 `.rej`/`.orig`.
- `make target/linux/prepare V=s`: RC=0 (1,198 patching lines, 0 rej/orig).
- `make target/linux/compile V=s -j16`: RC=0, **0 warnings** under
  `CONFIG_WERROR=y`; `CONFIG_LUOFU_SELECTOR=y` built-in
  (`luofu_selector_probe` etc. in `System.map`); zImage 3,262,472 B sha256
  `a4e4f6c0612317dd1b4ea98f86408de811f6af8635c419b5d9fc7113c1ed5b2e`;
  `_text = stext = 0xc0608000` and `DTEXT_OFFSET=0x00608000` (TEXT_OFFSET
  contract unchanged).
- `make ARCH=arm dt_binding_check
  DT_SCHEMA_FILES=Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-boot-selector.yaml`:
  RC=0 (`yamllint` unavailable, that lint step skipped as in earlier lanes).
- stock DTS compile (cpp + kernel dtc): RC=0, **zero warnings**, DTB 10,664 B
  sha256 `5726f7cd3798113a0881c86a9e2778860b5171ef2ee29fbb93970a6bb8a6b857`;
  `dt-validate` RC=0 with no `boot-selector` diagnostic (the pre-existing
  draft-DTS uart diagnostics remain, unrelated to this node).
- config canonical re-derivation, two runs: RC=0/RC=0, byte-identical
  (`4106877fdb6dad7ee9d5d2895ed6eb878da0c6a70ff4067d664be3da380a2a1f`) and
  symbol-identical to the delivered delta (309/309 symbols).
- `checkpatch.pl`: 0 errors / 0 warnings on the driver (251 lines), the YAML
  (59 lines) and the ABI doc (37 lines).
- `sh -n` on the installed `platform.sh`: RC=0; `bash -n` on the harness: RC=0.
- `tools/test-platform-abinstall.sh`: 52/52 fixture assertions; 58/58 with
  `--container` on the real built sysupgrade.bin (6,185,262 B sha256
  `0303d76a987fd16aeee7833db0d8188d6390129c62e1fc048560a971b922dce2`,
  the artifact recorded by the image-recipe lane).

## 7. Proven vs unverified

Proven (host-only): the selector values and the read/store interface; patch
`017` applies to pristine `linux-6.18.60` and after `001`-`016`; the full
series prepares and compiles with 0 warnings under `CONFIG_WERROR=y`;
`dt_binding_check` passes; the board DTS compiles with zero warnings; the
config delta is idempotent and symbol-identical; `checkpatch` is clean; the
mocked install path performs the right sequence and refuses every bad input
tested.

Unverified (hardware): the in-boot cell value after a real A/B switch, the
loader's env re-stamp, NAND write/read-back under ECC, bad-block handling,
the first boot of the written pair, rollback, and the interaction between the
selector store and the machine-restart driver's write-same. The container has
never booted and no byte has been flashed.

## 8. Remaining hardware TODOs

1. One attended A/B validation: upgrade -> boot the written pair -> confirm
   `ubi0` attaches the other rootfs slot and the cell reads the value the
   install path wrote; then switch back and boot the original pair (rollback).
2. Read the cell in-boot on both pairs before/after the loader runs, and
   `fw_printenv bootflag` afterwards, to close the env re-stamp question.
3. Confirm `ubiformat` on this NAND geometry and the raw `mtd write`
   kernel-slot path under the FMC driver's ECC, including a bad-block case.
4. Confirm the ramfs copy list ships `mtd`/`ubiformat`/`sha256sum`/`mktemp`/
   `head` and that `platform_check_image` re-runs there (sha256sum over the
   image inside the ramfs).
5. Measure the 0x21 manual/recovery in-boot state so the installer can decide
   whether a vendor-switched box may be upgraded without first normalizing the
   cell.
6. Decide and test the first-install path (TARGET-DESIGN.md section 5.5); the
   A/B path implemented here is an *upgrade* path and must not be sold as a
   factory installer.
