# SPDX-License-Identifier: GPL-2.0
# Convenience wrapper; the canonical build line is documented in Kbuild and
# implemented (with reporting) in ../tools/build.sh.

KDIR ?= /home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
STAGING_DIR ?= /home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
CROSS_COMPILE ?= $(STAGING_DIR)/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

all:
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
