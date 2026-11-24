# SPDX-License-Identifier: GPL-2.0-only
#
# target/linux/hisilicon/image/Makefile (OpenWrt main layout)
#
# STATUS (2026-11-24): THE IMAGE CHAIN IS REAL AND HAS BEEN BUILT MANY TIMES;
# IMAGES FROM THIS LINE HAVE BOOTED ON THE UNIT.  image/luofu.mk carries the
# device stanza and the target-specific Build stages (uImage -> FIP slice ->
# manifest -> rootfs UBI -> sysupgrade container); base-files/lib/upgrade/
# platform.sh carries the on-device checks and writes only the inactive pair
# (A/B selector), and base-files/etc/init.d/luofu_record_pair is the shipped
# boot-pair recorder.  The container is still NOT FLASHABLE as a product: the
# install/upgrade path has never run end to end on a device and no factory
# image exists.  Host build evidence: docs/SKELETON-SMOKE.md
# ("upstream-target session") and docs/incoming/wifi-upstream-target/REPORT.md.
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
