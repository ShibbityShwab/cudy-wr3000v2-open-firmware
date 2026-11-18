# SPDX-License-Identifier: GPL-2.0-only
#
# target/linux/hisilicon/luofu/image/Makefile
#
# STATUS: SCHEMATIC / UNTESTED / NOT A WORKING TARGET.
#
# Shape from TARGET-DESIGN.md sections 4.2 and 5. This is NOT the accepted
# OpenWrt image idiom and it is not buildable as it stands:
#   - the kernel platform patch and the drivers do not exist (4.1, 7);
#   - the FIP trailer is now generable (fip-slice.py make-trailer +
#     tools/build-kernel-slice.sh, 2026-11-18) but this recipe still treats it
#     as a fixed input; wiring + one attended boot are pending (5.2, 9);
#   - the sysupgrade metadata schema and platform.sh are not written (5.4);
#   - the factory/install image is an open question (5.5), so no image stanza
#     for it exists here on purpose.
#
# NON-NEGOTIABLE IMAGE FACTS (TARGET-DESIGN.md 3.2, 3.3, 5):
#   kernel slot = legacy uImage (-C none) + FIP trailer
#                 size <= 8650752 B (0x840000)
#                 gate magic 0xd7b7ab1e at 0x40 + ih_size + base-DTB totalsize
#   FIP trailer = base DTB + 96-byte descriptor + overlays. The bench lane uses
#                 a fixed 40,732 B artifact; the own-generated lane uses a
#                 DTB+overlay trailer (9,278 B in the first host build,
#                 2026-11-18). Generation is implemented; fabrication is never
#                 allowed (5.2).
#   rootfs slot = squashfs in a static UBI volume named "squashfs"
#                 size <= 24313856 B (0x1740000)
#   overlay     = JFFS2 on rootfs_data (kept; TARGET-DESIGN.md 5.3)
#   load/entry  = derived from the loader staging base and the kernel's
#                 TEXT_OFFSET, asserted at build time, never copied blind
#
# TODO(upstream idiom): device stanzas normally live in
# target/linux/<board>/image/<subtarget>.mk and are included by the target image
# infrastructure. The include lines are omitted on purpose because they differ
# between OpenWrt releases; verify them against the tree being targeted before
# copying.

# ---- geometry and budgets (from the measured layout) --------------------
KERNEL_SLOT_SIZE := 8650752
ROOTFS_SLOT_SIZE := 24313856
FIP_TRAILER_SIZE := 40732
UBI_PAGE_SIZE := 2048
UBI_PEB_SIZE := 131072
UBI_SUBPAGE_SIZE := 2048
UBI_VID_HDR_OFFSET := 2048
UBI_VOLUME_NAME := squashfs

# The stock loader stages the kernel slot at 0x83200000 and the decompressor
# computes (pc & 0xf8000000) + TEXT_OFFSET. With TEXT_OFFSET 0x00608000 this
# is 0x80608000 (TARGET-DESIGN.md 3.2). This value is the DERIVATION, not a
# safe constant: the real recipe must assert it against the kernel it builds.
LOADER_STAGE_ADDR := 0x83200000
KERNEL_TEXT_OFFSET := 0x00608000
KERNEL_LOADADDR := 0x80608000

# Path to the checked-in skeleton tool. Mapping: copy
# target-skeleton/tools/fip-slice.py to <openwrt>/scripts/fip-slice.py.
FIP_SLICE_TOOL ?= $(TOPDIR)/scripts/fip-slice.py
# The trailer is still an INPUT in this schematic recipe, but generation IS
# implemented (TARGET-DESIGN.md 5.2): fip-slice.py make-trailer derives the
# descriptor from user-supplied base DTB + overlays, and
# tools/build-kernel-slice.sh builds a complete own-artifact slice
# (uImage + base DTB + match overlay). Wiring it into this file and one
# attended boot remain (SKELETON-SMOKE.md, 2026-11-18 slice section).
FIP_TRAILER ?= $(KDIR)/fip-trailer-$(FIP_TRAILER_SIZE).bin

# ---- kernel image assembly ----------------------------------------------
# Flow (TARGET-DESIGN.md 5.2):
#   zImage [+ appended DTB]  -> mkimage -> uImage
#   uImage + FIP trailer     -> fip-slice.py assemble -> kernel slot
#   kernel slot              -> fip-slice.py manifest -> fip-manifest.txt
# Standard OpenWrt steps (append-dtb, Build/uImage) are not spelled out here;
# their names and order differ by release. The two target-specific calls are
# the ones this skeleton exists to pin down.
define Build/luofu-uimage
	$(STAGING_DIR_HOST)/bin/mkimage -A arm -O linux -T kernel -C none \
		-a $(KERNEL_LOADADDR) -e $(KERNEL_LOADADDR) \
		-n "Linux-$(LINUX_VERSION)" -d $@ $@.uimage
	mv $@.uimage $@
endef

# Assemble the kernel slot. fip-slice.py checks the uImage header, the trailer
# structure (FDT chain, descriptor constants/count/offsets, gate magic at the
# computed offset) and the slot budget, and fails the build on any mismatch.
# Trailer generation is done by make-trailer / build-kernel-slice.sh, not here.
define Build/fip-slice
	$(FIP_SLICE_TOOL) assemble \
		--uimage $@ \
		--trailer $(FIP_TRAILER) \
		--output $@.fipslice \
		--max-size $(KERNEL_SLOT_SIZE) \
		--expect-load $(KERNEL_LOADADDR) \
		--expect-entry $(KERNEL_LOADADDR)
	mv $@.fipslice $@
endef

# ---- sysupgrade metadata -------------------------------------------------
# Container layout (TARGET-DESIGN.md 5.4; SCHEMA NOT FROZEN):
#   CONTROL            board=..., target=..., version=...
#   kernel             full FIP slice
#   root               rootfs UBI image
#   fip-manifest.txt   ih_size, dtb_totalsize, gate offset+magic, container
#                      sha256, overlay sizes, slice sha256, slice length
# fip-slice.py manifest emits the bytes-derived subset of fip-manifest.txt
# from a validated slice. platform_check_image (target platform.sh, NOT
# written) must recompute these fields from the received bytes and refuse
# anything else - including a vendor recovery.bin.
define Build/fip-manifest
	$(FIP_SLICE_TOOL) manifest \
		--slice $(KDIR)/luofu-kernel-slice.bin \
		--output $(KDIR)/fip-manifest.txt
endef
# TODO(sysupgrade): the stock Build/sysupgrade-tar builds CONTROL and the two
# blobs but does not know about the FIP slice or fip-manifest.txt. A target
# wrapper (name is a placeholder) is needed to put the validated slice, the
# rootfs UBI and the manifest into the container; it cannot be written before
# the A/B selector protocol and metadata schema are closed (TARGET-DESIGN.md
# 5.4, 3.4).

# ---- rootfs UBI ----------------------------------------------------------
# Geometry from the measured rootfs slot (FLASH-PLAN.md 0.1; TARGET-DESIGN.md
# 5.1): p=131072 m=2048 s=2048 O=2048, static volume "squashfs". Input cfg
# sketch (TODO: create as a build input):
#   [squashfs]
#   mode=ubi
#   image=$(KDIR)/root.squashfs
#   vol_id=0
#   vol_type=static
#   vol_name=squashfs
define Build/luofu-rootfs-ubi
	$(STAGING_DIR_HOST)/bin/ubinize -o $@.ubi \
		-m $(UBI_PAGE_SIZE) -p $(UBI_PEB_SIZE) -s $(UBI_SUBPAGE_SIZE) \
		-O $(UBI_VID_HDR_OFFSET) $(KDIR)/luofu-ubinize.cfg
	mv $@.ubi $@
endef

# ---- device --------------------------------------------------------------
# Identity warning for the device page: the enclosure label may say v1; the PCB
# is the luofu v2 design, not the MediaTek v1 (HARDWARE-IDENTITY.md;
# TARGET-DESIGN.md 2).
define Device/cudy_wr3000-v2
	DEVICE_VENDOR := Cudy
	DEVICE_MODEL := WR3000
	DEVICE_VARIANT := v2.0 (HiSilicon luofu R116)
	DEVICE_DTS := luofu-r116
	DEVICE_DTS_DIR := ../dts
	KERNEL_SIZE := 8650752
	# MUST equal KERNEL_LOADADDR above (the (stage & 0xf8000000)+TEXT_OFFSET
	# derivation); the real recipe must assert it, not repeat it blindly.
	KERNEL_LOADADDR := 0x80608000
	# TODO(compatible): TARGET-DESIGN.md 4.2 uses cudy,wr3000-v2 here, but the
	# draft DTS root compatible is "hisilicon,luofu-r116". sysupgrade's board
	# match and the DTS compatible order must be made to agree (README open
	# questions).
	SUPPORTED_DEVICES += cudy,wr3000-v2
	IMAGES := sysupgrade.bin
	# SCHEMATIC: in the real recipe the stages are
	# append-dtb -> luofu-uimage -> (make-trailer + fip-slice) -> fip-manifest ->
	# luofu-sysupgrade-tar -> append-metadata. The host slice path exists
	# (tools/build-kernel-slice.sh) but the image recipe and rootfs/sysupgrade
	# wiring are not implemented.
	IMAGE/sysupgrade.bin := sysupgrade-tar | append-metadata
	# TODO(packages): final DEVICE_PACKAGES list (TARGET-DESIGN.md 4.2).
	# DEVICE_PACKAGES :=
	# TODO(factory): no factory/install image definition on purpose. The
	# install path is an open question (TARGET-DESIGN.md 5.5); do not add a
	# recovery/factory stanza until one path has been executed end to end on a
	# recovered unit.
endef
TARGET_DEVICES += cudy_wr3000-v2
