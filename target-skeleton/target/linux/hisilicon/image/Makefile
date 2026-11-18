# SPDX-License-Identifier: GPL-2.0-only
#
# target/linux/hisilicon/image/Makefile (OpenWrt main layout)
#
# STATUS: THE IMAGE CHAIN IS IMPLEMENTED AND BUILDS HOST-SIDE; IT HAS NEVER
# BOOTED. target/linux/hisilicon/image/luofu.mk carries the device stanza and
# the target-specific Build stages (uImage -> FIP slice -> manifest -> rootfs
# UBI -> sysupgrade container); base-files/lib/upgrade/platform.sh carries the
# on-device checks and refuses to write (A/B selector [U]). See ../README.md and
# the project's docs/SKELETON-SMOKE.md; the host build result and hashes are
# recorded there (image-recipe session).
#
# This file is the board-level image entry point: main includes the subtarget
# image definitions from $(SUBTARGET).mk (here: image/luofu.mk). The tree must
# also contain scripts/fip-slice.py and scripts/luofu-image.py (mapping:
# target-skeleton/tools/); the image build fails closed with an explicit
# message if they are missing.

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

DEVICE_VARS += KERNEL_LOADADDR KERNEL_ENTRY KERNEL_SIZE UIMAGE_NAME

include $(SUBTARGET).mk

$(eval $(call BuildImage))
