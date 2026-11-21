# BUILD-NOTES — hi5622 PCIe/ETE transport skeleton on Linux 6.18.60

**Result:** hi5622_pciet.ko (93,468 B, ELF32 ARM, license=GPL,
vermagic=6.18.60 SMP mod_unload ARMv7 p2v8), 2 objects / 44 text symbols, built with the
OpenWrt external toolchain (GCC 14.4.0) against the built kernel tree
openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60.
Build was CLEAN: zero warnings under the tree's -Werror.

**Build line** (see Kbuild in this dir; the .ko evidence copy lives at
/home/shibbityshwab/wifi-transport-port-work/build/hi5622_pciet.ko,
sha256 87fb532de76507e051685bd540e1f398c0b5e990ac317845fc57a28b02baddd5; it is NOT committed):

    export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    make ARCH=arm CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi- \
         -C $KDIR M=$(pwd) modules

**Source base:** none vendored.  All three C/header files in this dir are ORIGINAL text
written by the wifi-transport-port lane from the decoded interface contract
(wifi-driver-re §3-4, wifi-firmware-re §2-3, phase18 A.4-A.6, phase42 E.2-E.3); the
oal/hcc type definitions of the GPL framework were used as naming reference only.  No
vendor source, object code, firmware blob or calibration data is committed anywhere.

## Implementation notes (contract-first, not a vendor port)

1. Register/window map: every device CA is expressed as a BAR0 offset via the region-3
   rule (0x3b8000 + (CA - 0x40000000)) and the module derives BARs from the PCI core at
   probe time - no hardcoded host addresses, works for both RC0 and RC1 endpoints.
2. Endpoint iATU: six viewports at BAR2 + 0x104 + 0x200*i, programmed in the decoded
   vendor order with read-back verification (phase18 discipline).
3. Firmware upload: firmware_request_nowarn() + chunked __iowrite32_copy() to
   BAR0+0x6f8000, chunks of 0x80000, alias read-back check at BAR0+0x40000.
4. Boot: 0x5a5a -> CA 0x40000108, 0xcece -> CA 0x4000010c (host write site [U],
   own logged step).
5. H2D: pcie_msg_send_irq reproduction (mask 0x40039010, drain-poll 0x400392d4,
   doorbell write; twin word behind twin_enabled=0).
6. D2H: bitmap read/drain at 0x40039014 + per-id dispatch 0..10; ready handlers ids 1/2;
   polled scan because the luofu host driver cannot demux INTx yet (never-read misc
   window); optional request_irq path behind use_irq=1.
7. ETE rings: node arrays + 32 x 2 KiB buffers via dma_alloc_coherent(); SR0 group A,
   DR0 group B; node = {device-side buffer address, (len<<16)|0x6d2b}.  Device-side
   addresses always come from the DMA API - the vendor's host-VA==device-address
   identity is a 2G-userspace-split accident and is intentionally NOT copied
   (our 6.18 config is VMSPLIT_3G).
8. Safety: BUILD_BUG_ON asserts no writable offset equals the never-write out[5]
   (0x400392f0) + runtime gate on the single write helper; probe() performs no device
   writes; every device write is an explicit attended sysfs step.

## Build-time fixes needed (vs a naive port)

| # | issue | fix |
| --- | --- | --- |
| 1 | none required for kernel API drift - the module uses only current 6.18 APIs (firmware_request_nowarn, pcim_iomap_regions, dev_groups, sysfs_emit, kthread) | written against 6.18 from the start |
| 2 | %llx vs 32-bit resource_size_t on ARM | use %pap with &pdev->resource[n].start in the probe log |
| 3 | dev_printk.h not pulled by device.h on 6.18 | include <linux/dev_printk.h> explicitly |

## What is NOT compiled/attempted here

- luofu-host-618-delta.patch (dma-ranges on both pcie nodes) is NOT applied and NOT
  built - it is a documented bench prerequisite for ETE stage B2; stage B1 needs it not.
- The hcc message SEMANTICS, hdpp data path, alg, shuangta/hal feature layers - the next
  slices, sized in census/family-sizes.csv.
- The module has never been loaded on a device (host-only lane).

## License

All files in this dir are original GPL-2.0 text by this lane (matching the license of the
kernel they link against). Kbuild is build metadata; the .patch is a proposed change to
the project's own GPL-2.0 tree.
