# SPDX-License-Identifier: GPL-2.0-only
#
# target/linux/hisilicon/image/luofu.mk - image definitions for the luofu R116
# (Cudy WR3000 v2.0) OpenWrt target.
#
# STATUS: HOST-BUILD STAGES ARE REAL, THE RESULT HAS NEVER BOOTED.
#   - every stage below is wired into the stock OpenWrt image machinery and is
#     executed by the target install rule (image/Makefile + include/image.mk);
#   - the kernel slice, the rootfs UBI and the sysupgrade container are
#     validated from their own bytes at build time (tools/fip-slice.py checks
#     the decoded loader structure, tools/luofu-image.py re-parses the UBI and
#     cross-checks the container against fip-manifest.txt);
#   - NOTHING here has booted. One attended boot of a generated FIP trailer is
#     still required (TARGET-DESIGN.md 5.2, 9);
#   - the A/B boot selector (sysenv 0x10100C08) and the install path are [U]
#     (TARGET-DESIGN.md 3.4, 5.4, 5.5). The container is a documented build
#     artifact, NOT a flashable upgrade: base-files/lib/upgrade/platform.sh
#     refuses to write it and the factory/install stanza is deliberately
#     absent (section 5.5).
#
# WHAT THE TREE MUST CONTAIN (skeleton mapping, README section 4)
#   target/linux/hisilicon/image/luofu.mk           this file
#   target/linux/hisilicon/image/luofu-ubinize.cfg  rootfs ubinize cfg (checked in)
#   target/linux/hisilicon/Makefile                 must set KERNELNAME:=zImage
#   target/linux/hisilicon/dts/luofu-r116.dts             <- target-skeleton/dts/luofu-r116.dts
#   target/linux/hisilicon/dts/luofu-r116-overlay.dtso    <- target-skeleton/dts/luofu-r116-overlay.dtso
#                                                          (renamed to .dtso: OpenWrt compiles
#                                                          DEVICE_DTS_OVERLAY sources with the
#                                                          overlay rule, which requires .dtso)
#   <openwrt>/scripts/fip-slice.py       <- target-skeleton/tools/fip-slice.py
#   <openwrt>/scripts/luofu-image.py     <- target-skeleton/tools/luofu-image.py
#
# NON-NEGOTIABLE IMAGE FACTS (TARGET-DESIGN.md 3.2, 3.3, 5)
#   kernel slot = legacy uImage (-C none) + FIP trailer, <= 8,650,752 B
#                 gate magic 0xd7b7ab1e at 0x40 + ih_size + base-DTB totalsize
#   FIP trailer = base DTB + 96-byte descriptor + overlays. Generated here from
#                 OUR board DTB + OUR loader-match overlay with
#                 fip-slice.py make-trailer (fabrication of a vendor trailer is
#                 never allowed; the 40,732 B bench artifact is not used).
#   load/entry  = (loader staging base & 0xf8000000) + TEXT_OFFSET, derived
#                 below and asserted twice: against the built kernel _text
#                 symbol and again against the assembled uImage bytes.
#   rootfs slot = squashfs in a static UBI volume named "squashfs",
#                 p=131072 m=2048 s=2048 O=2048, <= 24,313,856 B
#   sysupgrade  = tar with CONTROL, kernel (FIP slice), root (rootfs UBI),
#                 fip-manifest.txt and INSTALL-STATUS.txt. The schema is NOT
#                 frozen (TARGET-DESIGN.md 5.4).

# ---- tool locations (skeleton mapping) ------------------------------------
LUOFU_FIP_SLICE := $(TOPDIR)/scripts/fip-slice.py
LUOFU_IMAGE_TOOL := $(TOPDIR)/scripts/luofu-image.py
LUOFU_PYTHON ?= python3

# ---- measured geometry and budgets (TARGET-DESIGN.md 3.2, 3.3, 5.1) -------
LUOFU_KERNEL_SLOT_SIZE := 8650752
LUOFU_ROOTFS_SLOT_SIZE := 24313856
LUOFU_UBI_PAGE_SIZE := 2048
LUOFU_UBI_PEB_SIZE := 131072
LUOFU_UBI_SUBPAGE_SIZE := 2048
LUOFU_UBI_VID_HDR_OFFSET := 2048
LUOFU_UBI_VOLUME_NAME := squashfs
LUOFU_UBINIZE_CFG := $(TOPDIR)/target/linux/hisilicon/image/luofu-ubinize.cfg

# ---- load/entry address: DERIVED, then asserted ---------------------------
# The stock loader stages the kernel slot at 0x83200000 and the ARM
# decompressor computes (pc & 0xf8000000) + TEXT_OFFSET; the platform patch
# (patches-6.18/002) sets textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000
# (TARGET-DESIGN.md 3.2). Never copy 0x80608000 without this derivation.
LUOFU_LOADER_STAGE := 0x83200000
LUOFU_TEXT_OFFSET := 0x00608000
LUOFU_LOADADDR := $(shell printf '0x%08x' $$(( ( $(LUOFU_LOADER_STAGE) & 0xf8000000 ) + $(LUOFU_TEXT_OFFSET) )))
ifneq ($(LUOFU_LOADADDR),0x80608000)
$(error luofu image: (loader stage $(LUOFU_LOADER_STAGE) & 0xf8000000) + TEXT_OFFSET $(LUOFU_TEXT_OFFSET) = $(LUOFU_LOADADDR), not the measured 0x80608000; the recipe refuses to embed an unverified load address)
endif

# ---- board DTB / loader-match overlay (built by compile-dtb) -------------
# DEVICE_DTS and DEVICE_DTS_OVERLAY in the device stanza below make the stock
# image machinery compile:
#   $(KDIR)/image-luofu-r116.dtb and $(KDIR)/image-luofu-r116-overlay.dtbo
LUOFU_BASE_DTB := $(KDIR)/image-luofu-r116.dtb
LUOFU_OVERLAY_DTBO := $(KDIR)/image-luofu-r116-overlay.dtbo
# decoded overlay match values (U-Boot FUN_800743ec; TRAILER-FORMAT.md 6)
LUOFU_OVERLAY_BOARD_ID := 3
LUOFU_OVERLAY_VENDOR_ID := hsan
LUOFU_OVERLAY_FAMILY := luofu

# side artifacts (validated inputs of the container stage)
LUOFU_TRAILER := $(KDIR)/luofu-fip-trailer.bin

# ---- 1. kernel: zImage -> uImage -> FIP slice -----------------------------
# The KERNEL chain is
#   kernel-bin | luofu-check-textofs | append-dtb | uImage none |
#   luofu-fip-slice | luofu-fip-manifest
# "uImage none" is the stock Build/uImage: it writes -a $(KERNEL_LOADADDR)
# -e $(KERNEL_ENTRY) (the device stanza sets both to the derived address) and
# -C none. Every target-specific step is a fail-closed Build macro.

# Guard the derivation against the kernel that was actually built: _text must
# carry the same physical TEXT_OFFSET the recipe derives. If patches-6.18/002
# or /003 are missing the kernel links at a different offset and the uImage
# load/entry would be a lie - refuse instead.
# _text is read from vmlinux with the target nm first: an incremental OpenWrt
# modules rebuild deletes $(LINUX_DIR)/System.map (Kernel/CompileModules/Default
# in include/kernel-defaults.mk), so System.map is only a fallback source.
define Build/luofu-check-textofs
	text=""; \
	if [ -f "$(LINUX_DIR)/vmlinux" ]; then \
		text="$$($(KERNEL_CROSS)nm "$(LINUX_DIR)/vmlinux" 2>/dev/null | awk '$$3 == "_text" { print $$1; exit }')"; \
	fi; \
	if [ -z "$$text" ] && [ -f "$(LINUX_DIR)/System.map" ]; then \
		text="$$(awk '$$3 == "_text" { print $$1; exit }' "$(LINUX_DIR)/System.map")"; \
	fi; \
	[ -n "$$text" ] || { \
		echo "luofu: cannot read _text from $(LINUX_DIR)/vmlinux or System.map; build the kernel first"; \
		exit 1; \
	}; \
	phys="$$(( 0x$$text & 0x0fffffff ))"; \
	want="$$(( $(LUOFU_TEXT_OFFSET) ))"; \
	if [ "$$phys" -ne "$$want" ]; then \
		echo "luofu: kernel _text=0x$$text (physical 0x$$(printf '%08x' $$phys)) does not match the derived TEXT_OFFSET $(LUOFU_TEXT_OFFSET)"; \
		echo "luofu: the uImage load/entry would not match the built kernel - apply patches-6.18/002+003 and rebuild"; \
		exit 1; \
	fi
endef

# Generate the FIP trailer from OUR base DTB + OUR loader-match overlay, then
# assemble uImage + trailer into the kernel slot. make-trailer requires the
# libfdt overlay apply the stock loader performs (fdtoverlay from the kernel
# build tree); assemble re-derives the load/entry address and enforces the
# slot budget, failing the build on any mismatch.
define Build/luofu-fip-slice
	[ -f "$(LUOFU_FIP_SLICE)" ] || { \
		echo "luofu: $(LUOFU_FIP_SLICE) missing; install target-skeleton/tools/fip-slice.py as scripts/fip-slice.py"; \
		exit 1; \
	}; \
	[ -f "$(LUOFU_BASE_DTB)" ] || { echo "luofu: base DTB $(LUOFU_BASE_DTB) missing"; exit 1; }; \
	[ -f "$(LUOFU_OVERLAY_DTBO)" ] || { echo "luofu: overlay DTBO $(LUOFU_OVERLAY_DTBO) missing (DEVICE_DTS_OVERLAY source target/linux/hisilicon/dts/luofu-r116-overlay.dtso)"; exit 1; }; \
	[ -x "$(LINUX_DIR)/scripts/dtc/fdtoverlay" ] || { echo "luofu: $(LINUX_DIR)/scripts/dtc/fdtoverlay missing; the loader's libfdt overlay apply must be verified, not skipped"; exit 1; }; \
	PATH="$(LINUX_DIR)/scripts/dtc:$$PATH" $(LUOFU_PYTHON) "$(LUOFU_FIP_SLICE)" make-trailer \
		--base-dtb "$(LUOFU_BASE_DTB)" \
		--overlay "$(LUOFU_OVERLAY_DTBO)" \
		--output "$(LUOFU_TRAILER)" \
		--expect-board-id $(LUOFU_OVERLAY_BOARD_ID) \
		--expect-vendor-id $(LUOFU_OVERLAY_VENDOR_ID) \
		--match-family $(LUOFU_OVERLAY_FAMILY) \
		--overlay-apply require \
		--force; \
	$(LUOFU_PYTHON) "$(LUOFU_FIP_SLICE)" assemble \
		--uimage "$@" \
		--trailer "$(LUOFU_TRAILER)" \
		--output "$@.fipslice" \
		--max-size $(LUOFU_KERNEL_SLOT_SIZE) \
		--loader-stage $(LUOFU_LOADER_STAGE) \
		--text-offset $(LUOFU_TEXT_OFFSET) \
		--force; \
	mv "$@.fipslice" "$@"
endef

# Emit fip-manifest.txt (bytes-derived fields) next to the kernel slice. The
# sysupgrade stage re-derives it from the bytes it is about to package, so a
# stale side file can never be shipped.
define Build/luofu-fip-manifest
	[ -f "$(LUOFU_FIP_SLICE)" ] || { echo "luofu: $(LUOFU_FIP_SLICE) missing"; exit 1; }; \
	$(LUOFU_PYTHON) "$(LUOFU_FIP_SLICE)" manifest \
		--slice "$@" \
		--output "$(KDIR)/$(DEVICE_NAME)-fip-manifest.txt" \
		--max-size $(LUOFU_KERNEL_SLOT_SIZE) \
		--target hisilicon/luofu \
		--device "$(DEVICE_NAME)" \
		--force
endef

# ---- 2. rootfs: squashfs -> static UBI volume -----------------------------
# Uses the checked-in ubinize cfg (volume squashfs, static, vol_id 0) and the
# measured geometry. tools/luofu-image.py re-parses the result: EC/VID header
# CRCs, layout volume table, volume type/name, every static-LEB data CRC and
# the rootfs slot budget. A UBI image that does not match fails the build.
define Build/luofu-rootfs-ubi
	[ -f "$(LUOFU_IMAGE_TOOL)" ] || { \
		echo "luofu: $(LUOFU_IMAGE_TOOL) missing; install target-skeleton/tools/luofu-image.py as scripts/luofu-image.py"; \
		exit 1; \
	}; \
	$(LUOFU_PYTHON) "$(LUOFU_IMAGE_TOOL)" rootfs-ubi \
		--rootfs "$(IMAGE_ROOTFS)" \
		--ubinize-cfg "$(LUOFU_UBINIZE_CFG)" \
		--output "$@.ubi" \
		--ubinize "$(STAGING_DIR_HOST)/bin/ubinize" \
		--source-date-epoch "$(SOURCE_DATE_EPOCH)" \
		--page-size $(LUOFU_UBI_PAGE_SIZE) \
		--peb-size $(LUOFU_UBI_PEB_SIZE) \
		--subpage-size $(LUOFU_UBI_SUBPAGE_SIZE) \
		--vid-hdr-offset $(LUOFU_UBI_VID_HDR_OFFSET) \
		--rootfs-slot-size $(LUOFU_ROOTFS_SLOT_SIZE) \
		--force; \
	mv "$@.ubi" "$@"; \
	cp -f "$@" "$(KDIR)/$(DEVICE_NAME)-rootfs.ubi"
endef

# ---- 3. sysupgrade container ---------------------------------------------
# Deterministic tar: CONTROL, INSTALL-STATUS.txt, fip-manifest.txt, kernel
# (FIP slice), root (rootfs UBI). The helper re-validates the slice against a
# manifest re-derived from the same bytes, validates the UBI again, and marks
# the artifact NOT FLASHABLE: the A/B selector protocol and the install path
# are [U] (TARGET-DESIGN.md 3.4, 5.4, 5.5). append-metadata then adds the
# standard OpenWrt image metadata. No factory/install stanza exists.
define Build/luofu-sysupgrade-container
	[ -f "$(LUOFU_IMAGE_TOOL)" ] || { echo "luofu: $(LUOFU_IMAGE_TOOL) missing"; exit 1; }; \
	[ -f "$(LUOFU_FIP_SLICE)" ] || { echo "luofu: $(LUOFU_FIP_SLICE) missing"; exit 1; }; \
	$(LUOFU_PYTHON) "$(LUOFU_FIP_SLICE)" manifest \
		--slice "$(IMAGE_KERNEL)" \
		--output "$(KDIR)/$(DEVICE_NAME)-fip-manifest.txt" \
		--max-size $(LUOFU_KERNEL_SLOT_SIZE) \
		--target hisilicon/luofu \
		--device "$(DEVICE_NAME)" \
		--force; \
	$(LUOFU_PYTHON) "$(LUOFU_IMAGE_TOOL)" sysupgrade \
		--kernel-slice "$(IMAGE_KERNEL)" \
		--rootfs-ubi "$@" \
		--manifest "$(KDIR)/$(DEVICE_NAME)-fip-manifest.txt" \
		--output "$@.container" \
		--fip-tool "$(LUOFU_FIP_SLICE)" \
		--board "$(DEVICE_NAME)" \
		--target hisilicon/luofu \
		--version "$(if $(REVISION),$(REVISION),$(VERSION_NUMBER))" \
		--source-date-epoch "$(SOURCE_DATE_EPOCH)" \
		--kernel-slot-size $(LUOFU_KERNEL_SLOT_SIZE) \
		--rootfs-slot-size $(LUOFU_ROOTFS_SLOT_SIZE) \
		--page-size $(LUOFU_UBI_PAGE_SIZE) \
		--peb-size $(LUOFU_UBI_PEB_SIZE) \
		--subpage-size $(LUOFU_UBI_SUBPAGE_SIZE) \
		--vid-hdr-offset $(LUOFU_UBI_VID_HDR_OFFSET) \
		--force; \
	mv "$@.container" "$@"
endef

# ---- device --------------------------------------------------------------
# Identity warning for the device page: the enclosure label may say v1; the PCB
# is the luofu v2 design, not the MediaTek v1 (HARDWARE-IDENTITY.md;
# TARGET-DESIGN.md 2). The runtime board_name is the DT root compatible, so
# SUPPORTED_DEVICES carries the DT compatibles (cudy,wr3000-v2 becomes valid
# once the machine descriptor's dt_compat list carries it - TODO in the machine
# patch; TARGET-DESIGN.md 4.2).
define Device/cudy_wr3000-v2
	DEVICE_VENDOR := Cudy
	DEVICE_MODEL := WR3000
	DEVICE_VARIANT := v2.0 (HiSilicon luofu R116)
	DEVICE_DTS := luofu-r116
	DEVICE_DTS_DIR := ../dts
	DEVICE_DTS_OVERLAY := luofu-r116-overlay
	KERNEL_NAME := zImage
	KERNEL_LOADADDR := $(LUOFU_LOADADDR)
	KERNEL_ENTRY := $(LUOFU_LOADADDR)
	KERNEL_SIZE := $(LUOFU_KERNEL_SLOT_SIZE)
	UIMAGE_NAME := Linux-$(LINUX_VERSION)
	KERNEL := kernel-bin | luofu-check-textofs | append-dtb | uImage none | luofu-fip-slice | luofu-fip-manifest
	FILESYSTEMS := squashfs
	IMAGES := sysupgrade.bin
	SUPPORTED_DEVICES += hisilicon,luofu-r116 hisilicon,luofu
	IMAGE/sysupgrade.bin := luofu-rootfs-ubi | luofu-sysupgrade-container | append-metadata
	# TODO(packages): final DEVICE_PACKAGES list (TARGET-DESIGN.md 4.2).
	# DEVICE_PACKAGES :=
	# TODO(factory): no factory/install image definition on purpose. The
	# install path is an open question (TARGET-DESIGN.md 5.5); do not add a
	# recovery/factory stanza until one path has been executed end to end on a
	# recovered unit.
endef
TARGET_DEVICES += cudy_wr3000-v2
