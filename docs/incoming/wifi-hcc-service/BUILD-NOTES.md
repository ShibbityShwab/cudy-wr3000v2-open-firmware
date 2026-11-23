# BUILD-NOTES - hi5622_pciet_hcc.ko (host HCC service module)

**Lane:** wifi-hcc-service (task-87, host-only). **Date:** 2026-11-24.
**Base:** verbatim copies of the wifi-transport-port clean-room transport
skeleton (../wifi-transport-port/build/) plus the new HCC service layer
(src/hi5622_hcc_service.h + src/pciet_hcc_service.c) and four marked
integration hunks in src/pciet_main.c and one field/prototype block in
src/hi5622_pciet.h.  No other lane's directory is modified.

## Result

| item | value |
| --- | --- |
| module | hi5622_pciet_hcc.ko |
| sha256 | 8fd9085431ee6b064f435f9550d9dfef5d74b0023020983682361af7746a48db |
| size | 201,140 B |
| ELF | 32-bit LSB relocatable, ARM, EABI5 (with debug_info, not stripped) |
| license | GPL |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| depends | (none) |
| module params | hcc_poll_ms (uint, default 5), hcc_cece_mode (uint, default 0), plus the inherited base params poll_ms / twin_enabled / use_irq / fw_path (poll_ms defaults 0 in this build) |
| PCI alias | pci:v000059E7d00000005sv*sd*bc*sc*i* |
| build cleanliness | zero compiler errors/warnings under the tree's CONFIG_WERROR, modpost clean |
| sysfs attrs | hcc_arm hcc_loop hcc_service hcc_sr_announce hcc_cece_kick hcc_dr_republish hcc_clear (WO); hcc_status hcc_ready hcc_irq_probe hcc_events hcc_rings hcc_plan (RO) - 13 total |

The .ko is built evidence only (kept at
/home/shibbityshwab/wifi-hcc-service-work/build/hi5622_pciet_hcc.ko) and is
**not committed** (the lane .gitignore covers *.ko/*.o/module build products;
no binary artifact is added to tracked text).

## Build line (module only - NEVER an image / make world build)

    export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    make ARCH=arm CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi- \
         -C $KDIR M=$(pwd) modules

The convenience wrapper src/Makefile runs exactly that line; the lane build
driver runs it against a scratch copy:

    ./tools/build.sh

Verified clean build 2026-11-24 (host).  Compiler:
arm-openwrt-linux-muslgnueabi-gcc (OpenWrt GCC 14.4.0 toolchain) 14.4.0.

### 6.18 API drift handled in the new file

| issue | fix |
| --- | --- |
| vsnprintf moved to <linux/sprintf.h> in 6.18 | the new file includes <linux/kernel.h>, which includes sprintf.h |
| hex_to_bin/hex2bin moved to <linux/hex.h> | linux/kernel.h includes it; used for the frame parser |
| cpu_relax() | include <linux/processor.h> (ARM defines it via asm/vdso/processor.h) |
| byteorder/generic.h must come after <linux/types.h> | include <linux/types.h> first; no direct byteorder header needed (linux/kernel.h pulls asm/byteorder.h) |
| CONFIG_MODULE_STRIPPED=y strips MODULE_DESCRIPTION | description kept in a non-stripping MODULE_INFO tag (same pattern as the B3 lane) |

## Self-test (device-free)

    python3 tools/frame_selftest.py
    -> wifi-hcc-service frame model selftest: 83 checks PASS
    -> default liveness frame: 000100000c00000000005a5a

The self-test re-implements the same pure frame validation/builder rules as
the kernel store and asserts: the vendor-validated default frame bytes; that
a 64-byte zero header fails the firmware tag/length rules; populated frame
field placement; all refusal classes (group 4, id 5, group != 0/2, dst range,
odd hex, payload > 0x674); packed ring-index arithmetic including the 32-buffer
pre-post value 0x400 and wrap counts.

## Integration hunks vs the base transport (all marked in the sources)

1. src/pciet_main.c: include hi5622_hcc_service.h and select driver name
   hi5622_pciet_hcc when built with -DHI5622_HCC_BUILD; base D2H poll default
   0 in this build.
2. src/pciet_main.c: refuse the four legacy device-write attrs that are
   incompatible with the corrected protocol - announce (blind 0xCECE write),
   ringinit (2-channel plain writes), send (64-byte zero-header frame), poll
   (unordered base scan).  Each refusal logs why and returns -EPERM.
3. src/pciet_main.c: pciet_hcc_probe() at the end of probe,
   pciet_hcc_remove() in remove.
4. src/hi5622_pciet.h: struct hi5622_hcc_service forward declaration,
   ctx->hcc pointer, probe/remove prototypes.

No device writes were added to probe (the HCC probe allocates host-DRAM
rings and the sysfs surface only).

## Not done / not proven by this lane

- The module has never been loaded on a device (host-only lane).
- The bench run is the Lead's attended session (BENCH-PROCEDURE.md).
- IRQ delivery remains the open question classified by hcc_irq_probe; this
  module deliberately does not try to force it.
