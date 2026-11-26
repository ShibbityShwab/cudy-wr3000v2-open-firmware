# The luofu DT bindings (extracted from the kernel patch series)

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Status:** extracted 2026-11-27 (goal round 64) from the driver patches in
`target-skeleton/target/linux/hisilicon/patches-6.18/`. These are the drafts/RFCs
a submission review must see as a separate bindings series (PR-READINESS gate 6),
not as unexplained additions inside the driver patches. The source patches are
unchanged; this directory is the review surface.

| binding | the source patch |
| --- | --- |
| hisilicon,luofu-boot-selector.yaml | 017-soc-luofu-boot-selector.patch |
| hisilicon,luofu-crg.yaml | 004-arm-clk-reset-luofu-crg.patch |
| hisilicon,luofu-fmc.yaml | 006-arm-mtd-nand-luofu-fmc.patch |
| hisilicon,luofu-gmac.yaml | 009-net-gmac-luofu.patch |
| hisilicon,luofu-mdio.yaml | 007-net-mdio-luofu.patch |
| hisilicon,luofu-pie.yaml | 015-net-pie-luofu.patch |
| hisilicon,luofu-pinctrl.yaml | 011-pinctrl-luofu.patch |
| hisilicon,luofu-reboot.yaml | 010-arm-luofu-restart.patch |
| hisilicon,luofu-switch.yaml | 016-net-lsw-luofu.patch |
| hisilicon,luofu-sysctrl.yaml | 010-arm-luofu-restart.patch |
| hisilicon,luofu-tvsensor.yaml | 012-arm-thermal-luofu-tvsensor.patch |
| hisilicon,luofu-wdt.yaml | 005-arm-watchdog-luofu-wdt.patch |

The `hisilicon,luofu-flashinfo.yaml` reserved-memory binding lives in
`docs/incoming/upstream-blockers/01-flashinfo-reserved-memory/` (already committed).