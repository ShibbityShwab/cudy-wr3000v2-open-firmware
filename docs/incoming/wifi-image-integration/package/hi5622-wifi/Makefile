# SPDX-License-Identifier: GPL-2.0-only
#
# OpenWrt package: clean-room Hi5622V100 Wi-Fi host driver + the target
# kernel's cfg80211 module for the BENCH image (lane: img-integ, task-112/132).
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
#     Installs hi5622_wifi.ko. Two build modes, chosen by what
#     tools/install-into-tree.sh staged into this package directory:
#
#     PREBUILT mode (hi5622_wifi.ko in ./prebuilt/):
#       The module is a LOCAL, ABI-validated bench artifact (e.g. the v7
#       AP-start driver 206,708 B sha fd8e1439...). It is copied as-is; the
#       package records its sha256 in prebuilt/prebuilt-sha256.txt. Nothing is
#       compiled, so the exact hardware-validated bytes are flashed.
#       NO BINARY IS STORED IN THE GIT REPOSITORY: install-into-tree.sh copies
#       the artifact from a local path into the (untracked) OpenWrt tree.
#
#     SOURCE mode (no ./prebuilt/): builds hi5622_wifi.ko from ./src, a source
#       snapshot of a committed driver tree (e.g. docs/incoming/wifi-cfg80211/
#       src) with $(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules.
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

# Build mode: prebuilt artifact if staged, else build from the src snapshot.
HI5622_KO := hi5622_wifi.ko
HI5622_PREBUILT_KO := $(CURDIR)/prebuilt/$(HI5622_KO)

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
# The clean-room driver: prebuilt ABI-validated artifact or ./src compile.
# ---------------------------------------------------------------------------
ifneq ($(wildcard $(HI5622_PREBUILT_KO)),)
  HI5622_MODE := prebuilt
  define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(CURDIR)/prebuilt/. $(PKG_BUILD_DIR)/
	# The kernel-package symvers export hook expects kbuild outputs; a
	# prebuilt module exports no symbols, so empty manifests are correct
	# (needed because the hook runs a fixed shell sequence).
	: > $(PKG_BUILD_DIR)/Module.symvers
	: > $(PKG_BUILD_DIR)/modules.order
  endef
  define Build/Compile
	# prebuilt, ABI-validated module: nothing is compiled or modified.
	@test -f $(PKG_BUILD_DIR)/$(HI5622_KO) || { \
		echo "ERROR: prebuilt $(HI5622_KO) missing from PKG_BUILD_DIR" >&2; exit 1; }
	@echo "hi5622-wifi: installing PREBUILT module $(HI5622_KO) (no compile)"
	@sha256sum $(PKG_BUILD_DIR)/$(HI5622_KO)
  endef
else
  HI5622_MODE := source
  define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(CURDIR)/src/. $(PKG_BUILD_DIR)/
  endef
  define Build/Compile
	+$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules
  endef
endif

define KernelPackage/hi5622-wifi
  SUBMENU:=Wireless Drivers
  TITLE:=Hi5622V100 clean-room Wi-Fi host driver (bench)
  DEPENDS:=@TARGET_hisilicon_luofu +kmod-hi5622-cfg80211
  FILES:=$(PKG_BUILD_DIR)/$(HI5622_KO)
  KCONFIG:=CONFIG_CFG80211
  # deliberately no AUTOLOAD: bench policy, load by hand
endef

define KernelPackage/hi5622-wifi/description
  Clean-room host driver for the Hi5622V100 Wi-Fi endpoint on the Hi5671Y
  (Cudy WR3000 v2.0) PCIe link: endpoint transport, firmware upload and
  release-only boot (0x5A5A, never 0xCECE during boot), HCC duplex protocol,
  direct-read device group-A D2H, cfg80211 full-MAC interface and the AP-start
  feature set. BENCH IMAGE ONLY. The vendor firmware is not included; point
  fw_path at a local copy at bench time. Loading the module performs no device
  writes. Build mode: $(HI5622_MODE).
endef

$(eval $(call KernelPackage,hi5622-cfg80211))
$(eval $(call KernelPackage,hi5622-wifi))
