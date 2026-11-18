# SPDX-License-Identifier: GPL-2.0-only
# SCHEMATIC: target/linux/hisilicon/image/Makefile (OpenWrt main layout)
# Validated only to the extent that `make defconfig` in openwrt main accepted the target and
# generated the device symbol (smoke test 2026-11-18); no image has been built. See ../README.md
# and docs/SKELETON-SMOKE.md.
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

DEVICE_VARS += KERNEL_LOADADDR KERNEL_SIZE

include $(SUBTARGET).mk

$(eval $(call BuildImage))
