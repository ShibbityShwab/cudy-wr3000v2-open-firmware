# SPDX-License-Identifier: GPL-2.0-only
#
# target/linux/hisilicon/Makefile
#
# STATUS: SCHEMATIC / UNTESTED / NOT A WORKING TARGET.
#
# First-pass shape from opensource/docs/TARGET-DESIGN.md sections 4-5. Nothing
# in target-skeleton/ has been built inside an OpenWrt tree; do not read this
# file as a buildable target. See ../README.md for the status and the mapping.
#
# Mapping: copy this directory to <openwrt>/target/linux/hisilicon/.
#
# What it still needs before it can build (none of it exists yet):
#   - the kernel platform patch (TARGET-DESIGN.md 4.1):
#       arch/arm/mach-hisilicon/Kconfig        ARCH_HISI + ARCH_LUOFU
#       arch/arm/mach-hisilicon/Makefile       machine-$(CONFIG_ARCH_LUOFU) += luofu
#       arch/arm/boot/dts/Makefile             dtb-$(CONFIG_ARCH_LUOFU) += luofu-r116.dtb
#       arch/arm/Makefile                      textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000
#   - the machine descriptor (clean mach-luofu; TARGET-DESIGN.md 7.1);
#   - the driver + dt-binding series (TARGET-DESIGN.md 7);
#   - a FIP trailer generator (TARGET-DESIGN.md 5.2). This skeleton cannot
#     generate the trailer; tools/fip-slice.py only checks/assembles one.
#   - the kernel config, re-derived against the chosen release (TARGET-DESIGN.md 4.2).

include $(TOPDIR)/rules.mk

ARCH:=arm
BOARD:=hisilicon
BOARDNAME:=HiSilicon ARM SoCs
FEATURES:=squashfs nand ubifs
SUBTARGETS:=luofu

# TARGET-DESIGN.md 4.1: follow the kernel of the OpenWrt release under
# development; do not hardcode a kernel in code. 6.12 was the release under
# development in the 2026-10-01 snapshot (PORT-PLAN.md); re-verify before use.
# The only kernel this project has lab evidence for is 5.10.201, which is why
# luofu/config-5.10 is committed as a REFERENCE - it is not the final config.
KERNEL_PATCHVER:=6.12

include $(INCLUDE_DIR)/target.mk

# TODO(final package list - TARGET-DESIGN.md 4.2). Keep this deliberately
# short and conservative: do not add a package for a driver or feature that
# does not exist yet, and do not let kmod-mtd-rw stand in for a real update
# path (the sysupgrade recipe is TARGET-DESIGN.md 5.4, and it is not written).
DEFAULT_PACKAGES += kmod-mtd-rw

$(eval $(call BuildTarget))
