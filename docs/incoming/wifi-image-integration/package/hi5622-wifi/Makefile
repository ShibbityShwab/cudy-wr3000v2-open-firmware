# SPDX-License-Identifier: GPL-2.0-only
#
# OpenWrt package: clean-room Hi5622V100 Wi-Fi host driver + the target
# kernel's cfg80211 module for the BENCH image (lane: img-integ, task-112).
#
# Two KernelPackages in one Makefile, on purpose:
#
#   kmod-hi5622-cfg80211
#     Installs net/wireless/cfg80211.ko as built by the target kernel tree
#     itself (target/linux/hisilicon/luofu/config-6.18 sets CONFIG_CFG80211=m).
#     The clean-room driver was compiled against that kernel tree, so the
#     bench image must load THAT cfg80211, not the mac80211 backports one.
#     OpenWrt's package/kernel/mac80211 also defines a package kmod-cfg80211
#     (cfg80211 from the backports tarball); exactly one of the two may be
#     selected. Selecting both would install two different cfg80211.ko files.
#
#   kmod-hi5622-wifi
#     Builds hi5622_wifi.ko from ./src, a source snapshot of
#     opensource/docs/incoming/wifi-cfg80211/src (cfg80211 full-MAC M0-M2 over
#     the working HCC duplex protocol). The snapshot is placed here by
#     tools/install-into-tree.sh together with a src-sha256.txt manifest; no
#     binary is stored in the repository.
#
# SAFETY / BENCH POLICY
#   * No AUTOLOAD and no init script: the module is loaded by hand in an
#     attended session. Probe allocates the wiphy and host rings but performs
#     no device writes and sends no H2D message; nothing may touch the Wi-Fi
#     chip without an operator.
#   * No vendor firmware, no calibration bytes, no custom-block data: the
#     vendor firmware image stays local evidence and is copied at bench time
#     (fw_path module parameter).
#   * BENCH IMAGE ONLY: this package is part of the bench image config delta
#     (../config-delta.config), never of the release/target image stanza.

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=hi5622-wifi
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=Cudy WR3000 v2 reverse-engineering project (bench image only)
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

# ---------------------------------------------------------------------------
# cfg80211 from the target kernel tree (CONFIG_CFG80211=m). No Build/Compile:
# the module is produced by target/linux/compile, this package only installs
# it into the rootfs so the bench image carries it.
# ---------------------------------------------------------------------------
define KernelPackage/hi5622-cfg80211
  SUBMENU:=Wireless Drivers
  TITLE:=cfg80211 (target kernel tree) for the Hi5622 bench image
  DEPENDS:=@TARGET_hisilicon_luofu
  FILES:=$(LINUX_DIR)/net/wireless/cfg80211.ko
  KCONFIG:=CONFIG_CFG80211
endef

define KernelPackage/hi5622-cfg80211/description
  cfg80211 built as a module from the luofu target kernel tree
  (CONFIG_CFG80211=m in target/linux/hisilicon/luofu/config-6.18). The
  clean-room hi5622 driver imports cfg80211 symbols and must run against the
  module built from the same kernel tree it was compiled with. Do NOT select
  package/kernel/mac80211's kmod-cfg80211 in the same image.
endef

# ---------------------------------------------------------------------------
# The clean-room driver (source snapshot in ./src, Kbuild included).
# ---------------------------------------------------------------------------
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(CURDIR)/src/. $(PKG_BUILD_DIR)/
endef

define Build/Compile
	+$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules
endef

define KernelPackage/hi5622-wifi
  SUBMENU:=Wireless Drivers
  TITLE:=Hi5622V100 clean-room Wi-Fi host driver (cfg80211 full-MAC, bench)
  DEPENDS:=@TARGET_hisilicon_luofu +kmod-hi5622-cfg80211
  FILES:=$(PKG_BUILD_DIR)/hi5622_wifi.ko
  KCONFIG:=CONFIG_CFG80211
  # deliberately no AUTOLOAD: bench policy, load by hand
endef

define KernelPackage/hi5622-wifi/description
  Clean-room host driver for the Hi5622V100 Wi-Fi endpoint on the Hi5671Y
  (Cudy WR3000 v2.0) PCIe link: endpoint transport, firmware upload and
  release-only boot (0x5A5A, never 0xCECE during boot), HCC duplex protocol,
  direct-read device group-A D2H, and the cfg80211 full-MAC M0-M2 interface
  (wiphy + VAP lifecycle; scan/AP firmware messages still pending). BENCH
  IMAGE ONLY. The vendor firmware is not included; point fw_path at a local
  copy at bench time. Loading the module performs no device writes.
endef

$(eval $(call KernelPackage,hi5622-cfg80211))
$(eval $(call KernelPackage,hi5622-wifi))
