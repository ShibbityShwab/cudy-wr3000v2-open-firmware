# SPDX-License-Identifier: GPL-2.0-only
#
# target/linux/hisilicon/Makefile
#
# STATUS (2026-11-24): this directory IS the build source of the bench-verified
# 6.18 target line: 67 patches (001-060 + 062-067 + 013a), the board DTS, the
# image chain and a re-derived luofu/config-6.18 build inside an OpenWrt main
# tree (linux-6.18.60; see ../docs/SKELETON-SMOKE.md, "upstream-target session"
# and ../docs/incoming/wifi-upstream-target/REPORT.md). Images built from this
# line have booted on the unit: 2 CPUs, network, SSH, persistent overlay
# (../RELEASE-NOTES.md) and the 2026-11-24 Wi-Fi transport line (GP1/PD4).
#
# It is still NOT an upstream-accepted target and NOT a flashable public
# release: no target PR has been sent, the driver series is a review form of
# drafts/RFCs, the A/B install path has never run on a device end to end, and
# the Hi5622 Wi-Fi driver remains vendor-only/GPL-blocked.
#
# Mapping (copy the whole target-skeleton/ tree, not this file alone):
#   target/linux/hisilicon/        -> <openwrt>/target/linux/hisilicon/
#   dts/*.dts, *.dtso              -> <openwrt>/target/linux/hisilicon/dts/
#   tools/{fip-slice,luofu-image,pad-dtb}.py -> <openwrt>/scripts/
# See ../README.md section 4 for the full mapping and the lane REPORT for the
# 2026-11-24 build verification.

include $(TOPDIR)/rules.mk

ARCH:=arm
BOARD:=hisilicon
BOARDNAME:=HiSilicon ARM SoCs
FEATURES:=squashfs nand ubifs
SUBTARGETS:=luofu

# The image recipe wraps arch/arm/boot/zImage into the legacy uImage + FIP
# trailer. Kernel/CopyImage only copies the products named here into $(KDIR),
# so a board without KERNELNAME produces no zImage for the image stage.
KERNELNAME:=zImage

# TARGET-DESIGN.md 4.1: follow the kernel of the OpenWrt release under
# development; do not hardcode a kernel in code. 6.12 was the release under
# development in the 2026-10-01 snapshot (PORT-PLAN.md); re-verify before use.
# The only kernel this project has lab evidence for is 5.10.201, which is why
# luofu/config-5.10 is committed as a REFERENCE - it is not the final config.
KERNEL_PATCHVER:=6.18

include $(INCLUDE_DIR)/target.mk

# TODO(final package list - TARGET-DESIGN.md 4.2). Keep this deliberately
# short and conservative: do not add a package for a driver or feature that
# does not exist yet. ubi-utils is required by the A/B sysupgrade write path
# (platform_do_upgrade calls ubiformat); the ramfs copy list in
# package/base-files/files/lib/upgrade/stage2 asks for ubiformat, so the image
# must actually ship it. kmod-mtd-rw is not a substitute for it.
DEFAULT_PACKAGES += kmod-mtd-rw ubi-utils

$(eval $(call BuildTarget))
