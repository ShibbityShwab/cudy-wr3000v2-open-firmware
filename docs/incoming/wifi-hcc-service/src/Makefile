# Makefile - convenience wrapper for the wifi-hcc-service module.
# Kernel-module build only; this never builds an OpenWrt image ("make world" is
# forbidden) and never touches another lane's transport directories.
#
#   make            -> builds hi5622_pciet_hcc.ko against the luofu 6.18.60 tree
#   make clean      -> removes the module build products (source untouched)
#
# Override KDIR / STAGING_DIR on the command line or through the environment.

KDIR ?= /home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
STAGING_DIR ?= /home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
ARCH ?= arm
CROSS_COMPILE ?= $(STAGING_DIR)/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

export STAGING_DIR

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean

.PHONY: all clean
