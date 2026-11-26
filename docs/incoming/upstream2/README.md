# upstream2 lane - the upstream OpenWrt target/DTS/image recipe

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Lane:** task-243 (owner `upstream2`), host-only. **Date:** 2026-11-27.
**Status:** CLOSED with evidence (this directory); the target itself remains
NOT upstream-accepted and NOT a flashable public release.

## Why this lane exists

The concrete upstream OpenWrt target for the Cudy WR3000 v2.0 / HiSilicon
Hi5671Y "luofu" board R116 already exists as reviewed single sources in
[`target-skeleton/`](../../../target-skeleton): the target directory, the
715-line board DTS, the image recipe and the 66-patch kernel series. The
previous lane (`upstream-dts`, task-238) added a fail-closed assembler, a
target manifest and the board-DTS contract evidence, but stopped before it
landed its build evidence and its REPORT/SUMMARY (the files its README
references were never committed; task-238 is still `in_progress`).

This lane completes that hand-off without creating a fifth copy of anything:

| deliverable | path | what it is |
| --- | --- | --- |
| this README | `README.md` | lane scope and status |
| report | [REPORT.md](REPORT.md) | the four requested pieces, the evidence grades, the artifacts built in this lane, and the honest gap list for an `openwrt/openwrt` PR |
| summary | [SUMMARY.md](SUMMARY.md) | the one-page result |
| assembly re-verification | [validation/assembly/](validation/assembly/) | fresh `assemble-target.sh --check` / `--manifest` / `--verify` logs; the regenerated manifest is byte-identical to the committed task-238 manifest |
| DTS evidence | [validation/dts/](validation/dts/) | the board DTB compiled from the assembled target tree, the 24-check board contract re-run, provenance |
| build evidence | [validation/image-build/](validation/image-build/) | the full `make world` reconstruction of the device image, the artifacts and their hashes, and the image-chain inspection |
| PR gap list | [PR-READINESS.md](PR-READINESS.md) | what separates this build-verified target from an upstream submission (bindings, DTS review deltas, install path, dependency set) |

## Single-source rule

Nothing here is a second copy of the target, the DTS, the image recipe or the
patch series: they stay single-sourced in
[`target-skeleton/`](../../../target-skeleton) and are installed into the
OpenWrt tree by
[`../upstream-dts/tools/assemble-target.sh`](../upstream-dts/tools/assemble-target.sh)
(manifest: [../upstream-dts/validation/target-manifest.tsv](../upstream-dts/validation/target-manifest.tsv),
86 lines / 85 installed files).

## Honest boundary

A target that assembles and builds an image is **not** upstream progress: there
is no target PR, no accepted driver/DTS binding series, Wi-Fi is vendor-only
and GPL-blocked, and the A/B install path has never run on a device end to end.
Read [REPORT.md](REPORT.md) section 6 and [PR-READINESS.md](PR-READINESS.md)
before citing anything here as "upstream".
