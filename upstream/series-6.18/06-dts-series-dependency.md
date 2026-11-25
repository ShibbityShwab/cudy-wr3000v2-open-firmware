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
| sha256 | `6fa1c6901d40bc6788c7170fd99e13e4c6f1ecd07e552f3cd8ba093a78b95b01` (**v4, current 2026-11-29**: the v4 board DTS adds the RC0 `reset-gpios` endpoint reset lines (+timings) consumed by the folded PCIe driver; 716 lines, **703-line** filtered payload; the previous **v3** entry was `1fd1d10a...` (**v3, 2026-11-29**: the 2026-11-21 board DTS content above **plus** the 2026-11-22 enabled-PCIe/`dma-ranges` additions, with the retired 2026-11-22 `wifi-pcie-hog` gone since the 2026-11-24 GP1 line and the release-line-only W23 thermal delta removed by `tools/dts-upstream-filter.py`; the previous 2026-11-22 v2 entry was `33956380...` and still carried the retired hog; before that `886f3ad0...`/`c029fd44...`; the superseded active cleanup `d8afee63...` did not boot and was reverted) |
| applies to | pristine linux-6.18.60 (same baseline as the kernel form), after the kernel series for compilation because it uses the CRG clock-ID header added by kernel patch 0004 |
| adds | `arch/arm/boot/dts/hisilicon/luofu-r116.dts` (**703 lines** after the release-line W23 filter; the single-source target DTS is 715 lines as of v4) and the `dtb-$(CONFIG_ARCH_LUOFU) += luofu-r116.dtb` entry in `arch/arm/boot/dts/hisilicon/Makefile`.  Content: the 2026-11-21 board features + buttons/LEDs + loader hand-off contract, the 2026-11-22 enabled-PCIe/`dma-ranges` additions, and the **v4 2026-11-29 RC0 `reset-gpios` endpoint reset lines** (`<&porta 20/21/28 GPIO_ACTIVE_LOW>` plus `reset-assert-us`/`reset-deassert-us`); the retired `wifi-pcie-hog` is absent |
| status | **DRAFT DTS SERIES ENTRY**.  The loader hand-off contract and the 2026-11-21/22 board deltas are hardware-verified (images f6685b80, c3ef2a50 and 6e9c54d0; the 6e9c54d0 boot-test closed the flashinfo loader binding; SMP root cause in SMP-RE.md section 24).  The **v4 RC0 `reset-gpios` addition is an ACTIVE change that has NOT been boot-tested yet**: one attended boot (recovery armed) is required before DTS-0001 v4 replaces the GP1 line as the hardware-validated board state.  **Zero upstream blockers remain**; the remaining in-series driver/YAML review items are normal RFC submission content |

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

**Release-line filter (2026-11-22).**  The single-source DTS is also the
release-line DTS, so it carries release-line-only content.  The current case is
the W23 thermal delta (target patches 066/067: `reg = <0x14900500 0x408>` plus
hwver/trim/int-base-offset/addition-en/efuse-trim-en/`hisilicon,threshold-mc`).
The in-series thermal binding (patch 0012) documents only a 0x100-byte window
with `unevaluatedProperties: false`, so the W23 properties are not reviewable
in the L2 form.  `tools/dts-upstream-filter.py` is the single definition of
that release-line -> upstream DTS delta; `gen-dts-series-patch.py` applies it
(fails closed if the expected block is not found exactly once) and
`check-release-line.sh` compares DTS-0001 against the filtered target and fails
if W23 properties leak in.  Decision recorded in `07-dts-upstream-bindings.md`
section 8.

### 3.1 Base-DTB padding is part of the DTS/image contract (pad-dtb)

The stock U-Boot bootfip reads the overlay-container gate word as a 32-bit
load at `base_dtb + FDT-totalsize`.  When the LED/keys nodes were added
(2026-11-21), the compiled base DTB became unaligned (13,041 B at that time);
U-Boot data-aborted on the unaligned load, reset, and fell back to the stock
pair.  The image recipe therefore runs **`Build/luofu-pad-dtb`**
(`target-skeleton/tools/pad-dtb.py`) on the base DTB before
`append-dtb`: it pads to an 8-byte boundary, folds the padding into the
FDT totalsize, and asserts `len(dtb) == totalsize` and
`totalsize % 8 == 0`, so an unaligned blob cannot reach a trailer again.
For the 2026-11-22 DTS the measured sizes were **12,865 B -> 12,872 B**
(target DTB sha256 `d8b41ac3...`) and **12,697 B -> 12,704 B** for the filtered
DTS-0001 payload (sha256 `288a5c3f...`).  The **2026-11-29 v3**
regeneration re-measured both with the host system dtc (v1.8.1 - a different
dtc build from the 2026-11-22 kernel-dtc figures above, so compare the v3
numbers within a run): the release-line target DTB is **12,722 B** sha256
`40b71de5...` -> pad **12,728 B**, and the filtered DTS-0001 payload is
**12,554 B** sha256 `4f2e0e8d...` -> pad **12,560 B**; both totalsizes are still
unaligned, so the `Build/luofu-pad-dtb` step remains mandatory.  The 2026-11-21 values were
**12,678 B -> 12,680 B** (DTB sha256 `62574b11...`, pad sha256 `8d79794e...`);
the conservative step was byte-identical to the pre-clean DTB `49fe5c47...`,
and the five 2026-11-21 active deltas were boot-tested as images f6685b80 (TWD),
c3ef2a50 (MAC provisioning, switch order, compatible order) and 6e9c54d0
(flashinfo compatible-first).  The 2026-11-22 DTS additions (PCIe enablement,
GPIO hog, dma-ranges) are hardware-derived but the DTB carrying them has not yet
been re-flashed and boot-tested as a unit.  The cleanup record, the hardware
regression of the superseded active cleanup, the boot-tests and the zero-blocker
status are in `07-dts-upstream-bindings.md`.

The full measurement and rationale are in
`../../docs/TRAILER-FORMAT.md` **section 11** ("Base-DTB totalsize must be
8-byte aligned").  This is an **image-build requirement, not a DTS-0001
change**: the kernel DTS source is unchanged by padding.  The validator applies
the same check: if the compiled DTB is unaligned it runs `pad-dtb.py` on a
copy, verifies the padded size is aligned and that the loader hand-off contract
is unchanged, and records the requirement in its summary.

### 3.2 v4 board-DTS delta (2026-11-29): RC0 endpoint reset lines

The v4 change is the `013a` -> devicetree fold: the board lines that hold the
Wi-Fi endpoints in reset are now DT data on the RC0 node instead of hardcoded
GPIO0 register writes in the driver.  The single-source DTS grows from 702 to
**715 lines**: the `pcie@10160000` node gains

    reset-gpios = <&porta 20 GPIO_ACTIVE_LOW>,
                  <&porta 21 GPIO_ACTIVE_LOW>,
                  <&porta 28 GPIO_ACTIVE_LOW>;
    reset-assert-us = <20000>;
    reset-deassert-us = <100000>;

and the `gpio0` port comment now points to that property.  The pulse semantics
are those of the hardware-validated GP1 line (assert the lines, hold the reset,
release, settle, all before the root-complex clock and LTSSM start); the
devicetree-property *form* is new and is an **active devicetree change**: it
needs one attended boot on the unit before DTS-0001 v4 can be called
hardware-validated.  The release-line filter needs no change (the W23
thermal block is untouched); DTS-0001 is regenerated with the same command as
above and its payload is again byte-identical to the filtered single source.

## 4. Validation (part of `tools/check-release-line.sh`)

The validator now performs the DTS-0001 checks after the release-line and
target-DTS checks:

1. apply DTS-0001 to the L1-patched pristine tree: RC=0;
2. `cmp` the added board DTS against the **release-line-filtered** target DTS
   (`tools/dts-upstream-filter.py`): byte-identical, and fail if any W23
   release-line marker leaked into the entry;
3. `cpp` + `dtc` the added DTS: RC=0, stderr 0/0 bytes;
4. `fdtget` the two hand-off nodes and the CPU enable-method:
   `console = /serial@1010f000`, `compatible = "hsan,flashinfo_reserved"`,
   `reg = 80600000 4000`, `atag-offset = 1000`,
   `enable-method = "hisilicon,luofu-smp"`.

**2026-11-29 v4 result (current):** L1 release line **55/55** patch(1) RC=0
(55 include / 11 exclude; 152 file events; 3 offset hunks, 0 fuzz, 0 rejects;
strict `git apply` all 55); the board DTS now carries the RC0 `reset-gpios`
endpoint reset lines and compiled to **12,848 B** sha256 `1c200daf...` (8-byte
aligned in this host-dtc run).  **DTS-0001 v4** sha256 `6fa1c690...` (716
lines, 703-line filtered payload) applies RC=0, is byte-identical to the
filtered target DTS, compiles to **12,680 B** sha256 `2a5eb334...`
(8-byte aligned), passes the hand-off contract and the leak check; release-line
manifest sha256 `e077544b...`, INCLUDED sha256 `cd7ae90c...`.  The L2 form was
regenerated with the folded 0013 and re-validated 26/26 (patch(1) and git am,
identical trees, 3 offsets, 0 fuzz, checkpatch 0 errors / 73 warnings).  The
v4 board DTS is an **active devicetree change**: one attended boot (recovery
armed) is still owed before it replaces GP1 as the hardware-validated line.
Logs: `validation/release-line-v4/` and
`../../docs/incoming/upstream-v4/validation/`.

**2026-11-29 v3 result (historical):** L1 release line **56/56** patch(1) RC=0
(56 include / 11 exclude; 153 file events; 5 offset hunks, 0 fuzz, 0 rejects;
strict `git apply` **all 56 without fuzz**), logs `validation/release-line-v3/`;
DTS-0001 sha256 `1fd1d10a...` (703 lines, **690-line** filtered payload) applies
RC=0 to the L1 tree, is **byte-identical to the filtered target DTS**, compiles
(cpp+dtc RC=0, 0/0 stderr, DTB **12,554 B** sha256 `4f2e0e8d...`, pad
**12,560 B**), passes the hand-off contract checks, leak check clean, and the
added DTS has no `wifi-pcie-hog`; release-line manifest sha256 `17d714be...`,
`INCLUDED.txt` sha256 `da4966aa...` (013a added, 55 -> 56).  The L2 review form
re-ran unchanged: 26/26 `patch(1)` and `git am`, identical trees, 3 offset
hunks, 0 fuzz, checkpatch 0 errors / 73 warnings, `gen-series.py --check` clean.
Nothing was sent; the v2 30-patch form is still not generated (the 013a -> DT
`reset-gpios` fold is the remaining content decision).

**2026-11-22 result (historical):** L1 release line 55/55 patch(1) RC=0, 3 offset
hunks, 0 fuzz, 0 rejects, strict `git apply` 55/55; target board DTS compiles
(cpp+dtc RC=0, 0 stderr, DTB **12,865 B** sha256 `d8b41ac3...`, unaligned ->
pad **12,872 B**); **DTS-0001** (sha256 `33956380...`, 712 lines, 699-line
filtered payload) applies RC=0, is byte-identical to the filtered target DTS
(W23 delta stripped by the filter), compiles (DTB **12,697 B** sha256
`288a5c3f...`, pad **12,704 B**) and passes the same hand-off contract checks;
release-line manifest sha256 `98931166...` (row 013 refreshed for the final
patch 013); logs `validation/release-line/` refreshed, `validation/sha256sums.txt`
93/93 OK.  The L2 review form re-ran 26/26, identical trees, 0 errors / **73**
checkpatch warnings (patch 0013 alone: 0/17), and the PCIe binding
`dt_binding_check` is RC=0 (see `04-validation.md` section 32).  The board DTS did
not change in the final pass, so DTS-0001 is unchanged.

2026-11-21 result (kept as history; after the 052/053 + numbering cleanup and
the board feature refresh):

- L1 release line: 51/51 patch(1) RC=0, 3 offset hunks, **0 fuzz**, 0
  rejects; strict `git apply`: **all 51 patches apply without fuzz**;
- board DTS / DTS-0001 (current final): both compile to the same **12,678 B**
  DTB, sha256
  `62574b118afd0aaf445b03d7ed49c9e021be78d39049dcb0626b9130f94ca899`,
  with the contract present; DTS-0001's DTS is byte-identical to the target
  DTS; the totalsize is **unaligned**, so the pad-dtb build step is required
  and the validator exercises it on a copy (**12,680 B** sha256
  `8d79794e...`, contract preserved);
- boot-tests: TWD level-high (image f6685b80); MAC-nvmem removal, switch
  MMIO-first order and cudy compatible ordering (image c3ef2a50: 2 CPUs,
  `GIC-0 29 Level twd`, `board_name=cudy,wr3000-v2`, MACs ...:73/...:74,
  br-lan ping 15/15, no errors); flashinfo compatible-first (image 6e9c54d0:
  stock loader staging accepted the new first compatible, 2 CPUs, cmdline
  ubi.mtd=rootfsb + project marker, no errors).  The conservative step had
  compiled byte-identically to the pre-clean DTB (sha256 `49fe5c47...`);
- DTS-0001 sha256 for the current source:
  `886f3ad095fc075cdf746cb0372eaf28c912fef91592f2885a739a7211bdf74c`;
- L0 lab stack (62 files, 001-058 + 062-065): 62/62 patch(1) RC=0, 0 fuzz;
  strict `git apply` 62/62;
- source-patch hashes refreshed in the release-line manifest for
  `056` (`dev_info` -> `dev_dbg` for the periodic W19 relatch line;
  `054` content is unchanged); manifest sha256
  `2bb7413e8291214fe09ccd84920166fc668ae72007dcbcc91727bb29e30eea70`.

Logs: `validation/release-line/` (including `dts-series-apply.log` and the two
`dts-series-*.err` files).  No hardware was touched and no kernel build was
run for this validation.

## 5. Evidence map

- target board DTS: `../../target-skeleton/dts/luofu-r116.dts` (single source);
- root cause / measured contract: `../../docs/SMP-RE.md` sections 21-24;
- release line: `05-release-line.md`, `release-line/manifest.tsv`;
- validation: `validation/release-line/summary.txt` and
  `tools/check-release-line.sh`;
- release-line -> upstream DTS filter: `tools/dts-upstream-filter.py`
  (single definition; W23 thermal delta) and `tools/gen-dts-series-patch.py`;
- base-DTB padding measurement and image-build requirement:
  `../../docs/TRAILER-FORMAT.md` section 11, `target-skeleton/tools/pad-dtb.py`,
  `Build/luofu-pad-dtb` in `target-skeleton/target/linux/hisilicon/image/luofu.mk`;
- board feature bring-up: submodule commit `aff7e27` (DES/LED/keys DTS,
  pad-dtb, config, watchdog sysfs), verified image `7f270b8c`;
- kernel review form dependency: `commit-messages/0018-arm-mach-hisilicon-smp.txt`
  and `03-rationale-tree-placement.md` `4/`6.
