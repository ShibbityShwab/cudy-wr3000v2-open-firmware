# 06 - Upstream DTS series dependency (DTS-0001)

The kernel review form in `series/` is kernel-only.  SMP correctness, however,
depends on a devicetree contract the bootloader consumes before the kernel
runs: the two loader hand-off nodes (`/aliases/console` and
`/reserved-memory/flashinfo@80600000`).  This document is the explicit
dependency record for that content: the companion **DTS series entry
DTS-0001**, its ordering with kernel patch 0018, its single-source rule and
its validation.

## 1. The entry

| field | value |
| --- | --- |
| id | **DTS-0001** (DTS series patch 1; independent of the kernel `series/0001-0026` numbering) |
| file | `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch` |
| sha256 | `8ee4b8e71be21f5fc61dd7c48546ce84bfb77550a08b021bdf4804707f4ce115` (refreshed 2026-11-21 after the stock board_id=3 LED/keys overlay landed; previous was `73d93769...`) |
| applies to | pristine linux-6.18.60 (same baseline as the kernel form), after the kernel series for compilation because it uses the CRG clock-ID header added by kernel patch 0004 |
| adds | `arch/arm/boot/dts/hisilicon/luofu-r116.dts` (**1,329 lines**, byte-identical to the single-source target DTS, including the board buttons/LEDs) and the `dtb-$(CONFIG_ARCH_LUOFU) += luofu-r116.dtb` entry in `arch/arm/boot/dts/hisilicon/Makefile` |
| status | **DRAFT DTS SERIES ENTRY** - the board DTS is TODO-heavy and not merge-intent until the bindings/cleanup in `03-rationale-tree-placement.md` section 4 are done; the two loader hand-off nodes are final and measured (SMP-RE.md section 24) |

The two nodes that make this entry release-critical for SMP:

    aliases {
        serial0 = &uart1;
        serial1 = &uart0;
        console = &uart1;      /* vendor base DTB contract; bootfip staging */
    };

    reserved-memory {
        #address-cells = <1>;
        #size-cells = <1>;
        ranges;

        flashinfo@80600000 {
            compatible = "hsan,flashinfo_reserved";
            reg = <0x80600000 0x4000>;
            atag-offset = <0x1000>;
        };
    };

Without them the stock U-Boot bootfip hand-off staging aborts, CPU1 never
enters the loader pen-poll loop, and every (correct) kernel SMP release has no
consumer - the 060/061 sentinel evidence and the 2026-11-21 root cause in
`../../docs/SMP-RE.md` section 24.

The 2026-11-21 refresh also adds the stock board_id=3 overlay features to the
same single-source DTS (verified on hardware, image `7f270b8c`):

- **gpio-keys-polled**: reset = GPIO22, WPS = GPIO23, both active-low, 20 ms
  poll; `linux,code` KEY_RESTART (0x198) / KEY_WPS_BUTTON (0x211);
- **gpio-leds**: six `oem:green:*` LEDs - status = GPIO24, wan = GPIO25,
  lan = GPIO3, internet = GPIO2, wifi = GPIO0, wifi5g = GPIO1 (all active-low).

They are ordinary mainline bindings (no new binding file), and they ride in
DTS-0001 because DTS-0001 is the byte-identical board-DTS copy.  The associated
kernel-config additions (LEDS_GPIO, INPUT/EVDEV/KEYBOARD, KEYBOARD_GPIO_POLLED,
WATCHDOG_SYSFS; INPUT_HISI_POWERKEY off) are target config, not DTS-0001 content.


## 2. Dependency and posting order

- **Kernel patch 0018 is necessary but not sufficient.**  Its commit message
  and `03-rationale-tree-placement.md` section 6 now name DTS-0001 as the
  required companion.  0018 without the DTS contract is inert (UP boot if the
  enable-method is absent; no CPU1 if the hand-off nodes are absent).
- **Post DTS-0001 together with 0018** (same submission window, or the DTS
  entry first).  Posting 0018 alone would ship a patch whose headline feature
  cannot work on the board.
- **Why the DTS is not inside `series/`:** the kernel form is a kernel-patch
  form.  Adding a board DTS there would either be an orphan file or a
  hand-maintained duplicate of the target DTS.  Keeping DTS-0001 in
  `dts-series/` preserves the kernel-only form and makes the dependency
  explicit and reviewable in one place.
- **Future DTS series numbering:** DTS-0002 onwards covers the remaining
  bindings-driven nodes (pinctrl consumer states, FMC partitions, PCIe, switch
  windows, etc.) once their bindings are accepted.  DTS-0001 is the first board
  DTS patch, not the final board file; it grows in place.

## 3. Single source and regeneration

The single source of truth is `../../target-skeleton/dts/luofu-r116.dts` (the
target tree's board DTS, which the canonical image 974e6ca6 was built from;
hand-off nodes committed in submodule `5a1b5b3`).  DTS-0001 must be a
byte-identical copy of it, not a hand-maintained duplicate.  Regenerate with:

    python3 tools/gen-dts-series-patch.py \
        --pristine <extracted linux-6.18.60> \
        --target-dts ../../target-skeleton/dts/luofu-r116.dts \
        --out dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch

The generator is deterministic (same input -> same sha256) and is the only
supported way to refresh DTS-0001 after a board-DTS change.  The validator
enforces the single-source rule: the DTS added by DTS-0001 must compare
byte-identical to the target DTS.

### 3.1 Base-DTB padding is part of the DTS/image contract (pad-dtb)

The stock U-Boot bootfip reads the overlay-container gate word as a 32-bit
load at `base_dtb + FDT-totalsize`.  When the new LED/keys nodes were added
(2026-11-21), the compiled base DTB became **13,041 B with an unaligned
totalsize**; U-Boot data-aborted on that load, reset, and fell back to the
stock pair.  The image recipe therefore runs **`Build/luofu-pad-dtb`**
(`target-skeleton/tools/pad-dtb.py`) on the base DTB before
`append-dtb`: it pads to an 8-byte boundary, folds the padding into the
FDT totalsize, and asserts `len(dtb) == totalsize` and
`totalsize % 8 == 0`, so an unaligned blob cannot reach a trailer again.
Measured: 13,041 B -> **13,048 B**.

The full measurement and rationale are in
`../../docs/TRAILER-FORMAT.md` **section 11** ("Base-DTB totalsize must be
8-byte aligned").  This is an **image-build requirement, not a DTS-0001
change**: the kernel DTS source is unchanged by padding.  The validator applies
the same check: if the compiled DTB is unaligned it runs `pad-dtb.py` on a
copy, verifies the padded size is aligned and that the loader hand-off contract
is unchanged, and records the requirement in its summary.

## 4. Validation (part of `tools/check-release-line.sh`)

The validator now performs the DTS-0001 checks after the release-line and
target-DTS checks:

1. apply DTS-0001 to the L1-patched pristine tree: RC=0;
2. `cmp` the added board DTS against the target DTS: byte-identical;
3. `cpp` + `dtc` the added DTS: RC=0, stderr 0/0 bytes;
4. `fdtget` the two hand-off nodes and the CPU enable-method:
   `console = /serial@1010f000`, `compatible = "hsan,flashinfo_reserved"`,
   `reg = 80600000 4000`, `atag-offset = 1000`,
   `enable-method = "hisilicon,luofu-smp"`.

2026-11-21 result (after the 052/053 + numbering cleanup and the board
feature refresh):

- L1 release line: 51/51 patch(1) RC=0, 3 offset hunks, **0 fuzz**, 0
  rejects; strict `git apply`: **all 51 patches apply without fuzz**;
- board DTS / DTS-0001 (refreshed): both compile to the same **13,041 B**
  DTB, sha256
  `49fe5c471786862e1057409ab920f866312e5f93e77585179dc20cdaebfcf536`,
  with the contract present; DTS-0001's DTS is byte-identical to the target
  DTS; the totalsize is **unaligned**, so the pad-dtb build step is required
  and the validator exercises it on a copy (13,048 B, contract preserved);
- L0 lab stack (62 files, 001-058 + 062-065): 62/62 patch(1) RC=0, 0 fuzz;
  strict `git apply` 62/62;
- source-patch hashes refreshed in the release-line manifest for
  `056` (`dev_info` -> `dev_dbg` for the periodic W19 relatch line;
  `054` content is unchanged); manifest sha256
  `61158aa09209a4f08e41e638d4e44add7baf1ed9c7168cb2ee7c487e5c73115b`.

Logs: `validation/release-line/` (including `dts-series-apply.log` and the two
`dts-series-*.err` files).  No hardware was touched and no kernel build was
run for this validation.

## 5. Evidence map

- target board DTS: `../../target-skeleton/dts/luofu-r116.dts` (single source);
- root cause / measured contract: `../../docs/SMP-RE.md` sections 21-24;
- release line: `05-release-line.md`, `release-line/manifest.tsv`;
- validation: `validation/release-line/summary.txt` and
  `tools/check-release-line.sh`;
- base-DTB padding measurement and image-build requirement:
  `../../docs/TRAILER-FORMAT.md` section 11, `target-skeleton/tools/pad-dtb.py`,
  `Build/luofu-pad-dtb` in `target-skeleton/target/linux/hisilicon/image/luofu.mk`;
- board feature bring-up: submodule commit `aff7e27` (DES/LED/keys DTS,
  pad-dtb, config, watchdog sysfs), verified image `7f270b8c`;
- kernel review form dependency: `commit-messages/0018-arm-mach-hisilicon-smp.txt`
  and `03-rationale-tree-placement.md` `4/`6.
