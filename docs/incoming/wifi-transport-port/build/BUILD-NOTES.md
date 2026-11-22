# BUILD-NOTES — hi5622 PCIe/ETE transport skeleton on Linux 6.18.60

**Result:** hi5622_pciet.ko (ELF32 ARM, license=GPL, vermagic=6.18.60 SMP mod_unload
ARMv7 p2v8), 2 objects / 44 text symbols, built with the OpenWrt external toolchain
(GCC 14.4.0) against the built kernel tree
openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60.
Build was CLEAN: zero warnings under the tree's -Werror.  **Bench-built/loaded module sha256:
`416d1db0...` (short prefix; the .ko is not committed).**  The previous hash recorded here
(`87fb532d...`, 93,468 B) belongs to an earlier build and is **stale**.
**Updated 2026-11-22 after the attended bench:** the module was loaded on the board and the
transport contract was device-verified (stage A/B1/B2 - see "Bench status" below).

**Build line** (see Kbuild in this dir; the .ko evidence copy is kept outside the repo -
it is NOT committed; bench-built sha256 `416d1db0...`):

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
   BAR0+0x6f8000, chunks of 0x80000; **verify reads back the target window
   BAR0+0x6f8000 word-by-word over the whole blob** (phase18 discipline; the
   BAR0+0x40000 alias is only useful once the device is running).  Bench: diffs=0.
4. Boot: 0x5a5a -> CA 0x40000108, 0xcece -> CA 0x4000010c.  **Bench-executed:
   cpu_released=1, gate_announced=1, DEVICE READY (D2H id 2, bitmap 0x4).**  The device
   posts READY about 3 s after the release; the poll thread waits 2 s before the first
   scan and polls every 50 ms (commit 9700250).
5. H2D: pcie_msg_send_irq reproduction (mask 0x40039010, drain-poll 0x400392d4,
   doorbell write; twin word behind twin_enabled=0).  **Bench smoke passed:** 64 B on
   SR0, doorbell mask 8 / id 3, no abort.
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

## Bench status (2026-11-22) - transport contract device-verified

| stage | result |
| --- | --- |
| A - enumerate | both endpoints probe on the working 6.18 link (BAR0 0x40000000 / 0x58000000, BAR2 0x41800000 / 0x59800000); ETE rings allocated |
| B1.1 prepare | six endpoint iATU viewports programmed with the compact 7-word, absolutely-addressed layout; 6/6 read back OK, windows=1 |
| B1.2/B1.3 upload+verify | 928,920 B firmware to BAR0+0x6f8000; target-window verify **diffs=0** |
| B1.4/B1.5/B1.6 | 0x5a5a release (cpu_released=1), 0xcece announce (gate_announced=1), D2H bitmap 0x4 -> **DEVICE READY id 2** |
| B2 - ETE/H2D | RC inbound window from board DTS dma-ranges (vp0 0x80000000..0x8fffffff verified by the DWC framework); SR0/DR0 rings programmed; H2D smoke doorbell OK, no abort |

Source of the bench-built module is the committed `build/` directory (code + Kbuild);
the .ko is not committed.  Details: ../REPORT.md sections 0 and 2.

## What is NOT compiled/attempted here

- luofu-host-618-delta.patch (dma-ranges on both pcie nodes) is **superseded**: the board
  DTS now carries the identity `dma-ranges` (submodule commit 5a6a500) and it is
  bench-validated (image W26a).  Keep the patch as history, do not apply.
- The hcc message SEMANTICS, hdpp data path, alg, shuangta/hal feature layers - the next
  slices, sized in census/family-sizes.csv.
- ETE data movement itself (the rings are programmed and the H2D doorbell smoke passed, but
  no DR round trip/payload exchange has been exercised) and the ETE enable sequence
  (+0x00/+0x08/+0x48) remains [U].
- The module is a bench instrument, not a Wi-Fi driver: no cfg80211/mac80211 integration and
  no netdev/data path.

## License

All files in this dir are original GPL-2.0 text by this lane (matching the license of the
kernel they link against). Kbuild is build metadata; the .patch is a proposed change to
the project's own GPL-2.0 tree.
