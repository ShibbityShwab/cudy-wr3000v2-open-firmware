# upstream-dts lane - the upstream OpenWrt target for the luofu v2 board

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Lane:** task-238 (owner `upstream-dts`). **Host-only:** no bench contact, no
image flashed, no device access. **Date:** 2026-11-27.
**Status:** OPEN (this file is updated as the lane lands its evidence).

## What this lane delivers

The concrete upstream OpenWrt target for the Cudy WR3000 v2.0 / HiSilicon
Hi5671Y "luofu" board R116, assembled and build-verified from the project's
single sources, plus the honest gap list of what an `openwrt/openwrt` PR still
needs.

The four requested pieces already exist as reviewed sources in this submodule;
the missing piece was that they had **never been assembled and built as one
target tree since the 2026-11-29 v4 fold** (patch 013 `reset-gpios` fold +
715-line board DTS). This lane adds:

| deliverable | path | what it is |
| --- | --- | --- |
| assembler | [tools/assemble-target.sh](tools/assemble-target.sh) | one-command, fail-closed assembly of the target into an OpenWrt tree from the single sources (dry-run `--check`, `--apply DEST`, `--verify DEST`, `--manifest FILE`) |
| manifest | [validation/target-manifest.tsv](validation/target-manifest.tsv) | source path -> OpenWrt destination -> mode -> sha256 for every installed file (the target's table of contents) |
| validation | [validation/](validation/) | build logs, artifact hashes, DTB decompile, container inspection, tree provenance |
| report | [REPORT.md](REPORT.md) / [SUMMARY.md](SUMMARY.md) | what compiles/boots-is-verified, DTS coverage, recipe inspection, remaining gaps |

## Single-source rule (no fifth DTS copy)

The project has repeatedly paid for divergent DTS/patch copies. This lane does
**not** commit another copy of the board DTS, the patch series or the image
recipe: they stay single-sourced in
[`target-skeleton/`](../../../target-skeleton):

- target directory: `target-skeleton/target/linux/hisilicon/` (Makefile,
  Kconfig review stub, `luofu/` subtarget, `image/`, `base-files/`,
  `patches-6.18/`)
- board DTS: `target-skeleton/dts/luofu-r116.dts` + `luofu-r116-overlay.dtso`
- image recipe: `target-skeleton/target/linux/hisilicon/image/luofu.mk`
- helper scripts: `target-skeleton/tools/{fip-slice,luofu-image,pad-dtb}.py`

`assemble-target.sh` installs exactly those files, in the mapping documented in
`target-skeleton/README.md` section 4 (the mapping the previous lane applied by
hand). Running it twice is idempotent; `--verify` recomputes every hash.

## Honest boundary

A build-verified target tree is **not** upstream progress: there is no target
PR, no accepted DTS/driver series, no Wi-Fi support claim, and the install path
has never run end to end on the device. Read [REPORT.md](REPORT.md) for the
exact evidence grades and the gap list.
