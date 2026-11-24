# SPDX-License-Identifier: GPL-2.0-only
#
# OpenWrt package: clean-room Hi5622V100 PCIe transport + host HCC service.
#
# INTERIM PACKAGING (task-95, 2026-11-24): the kernel-module source snapshot in
# src/ is the compiled/bench-validated wifi-d2h-direct build of
# hi5622_pciet_hcc.ko (transport + vendor-order HCC service + direct-read D2H),
# assembled from the committed lanes
#   opensource/docs/incoming/wifi-hcc-service/src
#   opensource/docs/incoming/wifi-d2h-direct/src  (new files + integration patch)
# and verified byte-identical to that lane's build tree.  It is replaced
# wholesale when the clean driver tree (docs/incoming/wifi-driver-clean,
# task-94) lands: swap src/ and keep the same Kbuild object list.
#
# NO VENDOR BYTES: this package ships no firmware blob, no custom-block data
# and no vendor object.  The 928,920-byte vendor firmware image and both
# custom blocks are LOCAL EVIDENCE ONLY (project publication rule); see
# README.md for the local-only handling.  The package only provides the
# mechanism (fw_path, hi5622-seed) that consumes local evidence at bench time.
#
# SAFETY: loading the module runs probe only (allocates host rings + sysfs, no
# device writes).  Nothing here releases the CPU or writes the CECE gate; those
# remain attended, one-idea-at-a-time bench steps.

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=hi5622-pciet-hcc
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_MAINTAINER:=Cudy WR3000 v2 reverse-engineering project
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define KernelPackage/hi5622-pciet-hcc
  SUBMENU:=Wireless Drivers
  TITLE:=Hi5622V100 PCIe transport + host HCC service (bench, clean-room)
  DEPENDS:=@TARGET_hisilicon_luofu
  FILES:=$(PKG_BUILD_DIR)/hi5622_pciet_hcc.ko
  # Deliberately NO AUTOLOAD: the module must not be auto-bound on an
  # unrelated image, and the init script below is disabled by default.
endef

define KernelPackage/hi5622-pciet-hcc/description
  Clean-room host driver for the Hi5622V100 Wi-Fi endpoint on the Hi5671Y
  (Cudy WR3000 v2.0) PCIe link: endpoint iATU windows, firmware upload/verify,
  release-only boot (0x5A5A, never 0xCECE during boot), vendor-order HCC
  service, direct-read device group-A D2H receive path, and a guarded custom
  block pre-seed helper.  BENCH IMAGE ONLY: no vendor firmware or vendor
  binary is included; loading the module performs no device writes.
endef

define KernelPackage/hi5622-pciet-hcc/conffiles
/etc/config/hi5622-pciet
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(CURDIR)/src/. $(PKG_BUILD_DIR)/
	$(CP) $(CURDIR)/tools/hi5622-seed.c $(PKG_BUILD_DIR)/hi5622-seed.c
endef

define Build/Compile
	+$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules
	# The seed helper is a STATIC userspace binary: the kmod package must not
	# pull a libc dependency (and the bench image needs no extra runtime).
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -Wall -Wextra -Os -static -s \
		-o $(PKG_BUILD_DIR)/hi5622-seed $(PKG_BUILD_DIR)/hi5622-seed.c
endef

define KernelPackage/hi5622-pciet-hcc/install
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/etc/config $(1)/usr/sbin
	$(INSTALL_BIN) $(CURDIR)/files/hi5622-pciet.init $(1)/etc/init.d/hi5622-pciet
	$(INSTALL_CONF) $(CURDIR)/files/hi5622-pciet.config $(1)/etc/config/hi5622-pciet
	$(INSTALL_BIN) $(CURDIR)/files/hi5622-status $(1)/usr/sbin/hi5622-status
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/hi5622-seed $(1)/usr/sbin/hi5622-seed
endef

$(eval $(call KernelPackage,hi5622-pciet-hcc))
