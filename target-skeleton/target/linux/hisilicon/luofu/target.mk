# SPDX-License-Identifier: GPL-2.0-only
#
# target/linux/hisilicon/luofu/target.mk
#
# STATUS (2026-11-24): BUILD SOURCE of the bench-verified 6.18 release line
# (see ../Makefile); not an upstream-accepted target and not a public release.
#
# Subtarget: luofu (HiSilicon Hi5671Y, Cudy WR3000 v2.0 board R116).
# Shape from TARGET-DESIGN.md 4.1/4.2. Mapping: copy to
# <openwrt>/target/linux/hisilicon/luofu/target.mk.
#
# Why a subtarget and not a second target: the vendor firmware itself calls the
# platform "hisilicon/luofu" (TARGET-DESIGN.md 4.1), and the target/subtarget
# split is what would let a second board on this SoC family be added later.
# Only one board is in scope for the first submission.

BOARDNAME:=luofu (HiSilicon Hi5671Y)

# Cortex-A9 with PL310 L2; arm_cortex-a9 is already an upstream OpenWrt
# package architecture (the stock image uses it), so this adds no new feed
# architecture (TARGET-DESIGN.md 4.1).
CPU_TYPE:=cortex-a9

# NAND is the only boot medium on this board (128 MB SPI-NAND via the FMC
# controller; TARGET-DESIGN.md sections 2 and 3.3). The rootfs is squashfs in
# a static UBI volume and the shared overlay stays JFFS2 for the first
# submission (TARGET-DESIGN.md 5.3).
FEATURES+=nand

# TODO(packages): the ubifs userspace package is not required by the selected
# rootfs shape (squashfs in a static UBI volume + JFFS2 overlay); it is kept
# from the TARGET-DESIGN.md 4.2 sketch until the final package list is decided.
DEFAULT_PACKAGES += ubifs
