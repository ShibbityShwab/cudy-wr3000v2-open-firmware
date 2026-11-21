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
| sha256 | `73d93769c0cf5e63c17932c2a0f7285cd5fda59e4c93164c94a96e9cf24be788` |
| applies to | pristine linux-6.18.60 (same baseline as the kernel form), after the kernel series for compilation because it uses the CRG clock-ID header added by kernel patch 0004 |
| adds | `arch/arm/boot/dts/hisilicon/luofu-r116.dts` (1,266 lines, byte-identical to the single-source target DTS) and the `dtb-$(CONFIG_ARCH_LUOFU) += luofu-r116.dtb` entry in `arch/arm/boot/dts/hisilicon/Makefile` |
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
supported way to refresh DTS-0001 after a board-DTS change.

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

2026-11-21 result (release fine after the 052/053 and numbering cleanup):

- L1 release line: 51/51 patch(1) RC=0, 3 offset hunks, **0 fuzz**, 0
  rejects; strict `git apply`: **all 51 patches apply without fuzz**;
- board DTS / DTS-0001: both compile to the same **12,296 B** DTB, sha256
  `50daf3d0d50929aaa3af35fa40fae69a73428fd5b1c5bcc686795e89cb92d8eb`,
  with the contract present; DTS-0001's DTS is byte-identical to the target
  DTS;
- L0 lab stack (62 files, 001-058 + 062-065): 62/62 patch(1) RC=0, 0 fuzz;
  strict `git apply` 62/62.

Logs: `validation/release-line/` (including `dts-series-apply.log` and the two
`dts-series-*.err` files).  No hardware was touched and no kernel build was
run for this validation.

## 5. Evidence map

- target board DTS: `../../target-skeleton/dts/luofu-r116.dts` (single source);
- root cause / measured contract: `../../docs/SMP-RE.md` sections 21-24;
- release line: `05-release-line.md`, `release-line/manifest.tsv`;
- validation: `validation/release-line/summary.txt` and
  `tools/check-release-line.sh`;
- kernel review form dependency: `commit-messages/0018-arm-mach-hisilicon-smp.txt`
  and `03-rationale-tree-placement.md` `4/`6.
