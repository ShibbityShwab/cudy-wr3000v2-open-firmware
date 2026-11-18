# SPDX-License-Identifier: GPL-2.0-only
#
# target/linux/hisilicon/luofu/profiles/cudy_wr3000-v2.mk
#
# STATUS: SCHEMATIC / UNTESTED / NOT A WORKING TARGET.
#
# Device profile sketch. Modern OpenWrt builds images from the Device/ stanza in
# image/Makefile; the profile mechanism below is kept because TARGET-DESIGN.md
# 4.2 names a "profiles/ device profile" and because some releases still select
# a profile in .config (CONFIG_TARGET_PROFILE). TODO(upstream review): keep
# only what the targeted release uses.
#
# Package selection here is intentionally minimal. Do not add packages for
# drivers/features that do not exist (TARGET-DESIGN.md 7); the final list is
# TARGET-DESIGN.md 4.2 "TODO: final package list".

define Profile/CudyWR3000V2
	NAME:=Cudy WR3000 v2.0 (HiSilicon luofu R116)
	PACKAGES:=kmod-mtd-rw
endef

define Profile/CudyWR3000V2/Description
	Support for the Cudy WR3000 v2.0 (board R116, HiSilicon Hi5671Y luofu).
	SCHEMATIC: no driver, binding, image recipe or install path exists yet
	(TARGET-DESIGN.md 9). The enclosure label may say WR3000 v1; the PCB is
	the luofu v2 design, not the MediaTek v1 (HARDWARE-IDENTITY.md).
endef

$(eval $(call Profile,CudyWR3000V2))
