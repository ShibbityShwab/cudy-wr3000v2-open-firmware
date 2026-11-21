# SPDX-License-Identifier: GPL-2.0
# DRAFT Makefile for the wifi_shim prototype (build against the 6.18 built tree).
#
# Regeneration (host, from opensource/docs/incoming/wifi-shim-port):
#   K=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
#   CC=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-gcc
#   make KDIR=$K CROSS_COMPILE=".../arm-openwrt-linux-muslgnueabi-" -C $K M=$PWD modules
#
# NOTE: this module is a design draft; it has not been bench-built (the
# struct-layout audit returned NO-GO before bench compilation was needed).

obj-m += wifi_shim.o

ccflags-y += -Wno-declaration-after-statement
