# DTS validation - upstream2 lane

<!-- SPDX-License-Identifier: GPL-2.0-only -->

Host-only board-DTS evidence for the luofu R116 board (Cudy WR3000 v2.0), 2026-11-27.

| file | what |
| --- | --- |
| build-evidence.txt | source sha256, exact cpp+dtc command, kernel tree used, result and hashes |
| dts-check.log | the 24-check board contract (fidus/fdtget) run on the compiled DTB + vendor base-DT diff, 24/24 PASS |
| cpp-warnings.log, dtc-warnings.log, dtc-decompile-warnings.log | all empty (0 warnings) |

The source is single-sourced at ../../../../../target-skeleton/dts/luofu-r116.dts;
no DTS copy is committed here. Board-DTS background (node inventory, vendor
base-DT comparison, remaining upstream review gaps) is in
[../upstream-dts/DTS-COVERAGE.md](../../../upstream-dts/DTS-COVERAGE.md); this
lane only re-ran the evidence on the current tree and records it here.