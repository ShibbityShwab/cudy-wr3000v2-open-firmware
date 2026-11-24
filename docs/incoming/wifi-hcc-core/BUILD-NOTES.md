# BUILD-NOTES - wifi-hcc-core lane (task-92)

**Date:** 2026-11-24.  **Host-only:** no image build, no bench-router contact.
The .ko produced below is local build evidence and is NOT committed (the lane
.gitignore covers kernel/host build products).

## Result

| item | value |
| --- | --- |
| host build | gcc (GCC) 16.2.1 20260810, -std=c99 -O2 -Wall -Wextra -Werror -pedantic: **zero warnings** |
| host selftest | **528 checks, 0 failure(s), RESULT: PASS** (tests/hcc_core_test.c) |
| kernel compile check | module hcc_core_kmod.ko, 85,404 B |
| module sha256 | 96e16cbbfc10057f992ece5d9d36710a394fadb32c0fb77637a8edcdc5b02c15 |
| ELF | 32-bit LSB relocatable, ARM, EABI5 (with debug_info) |
| license / depends | GPL / none |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| kernel tree | /home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60 |
| cross compiler | arm-openwrt-linux-muslgnueabi-gcc (OpenWrt GCC 14.4.0) 14.4.0 |
| kernel build cleanliness | zero compiler warnings, modpost clean (the tree sets CONFIG_MODULE_STRIPPED=y, so the check module uses MODULE_INFO(description,...) to keep modpost quiet) |
| kernel build output | /tmp/wifi-hcc-core-build/kmod/hcc_core_kmod.ko (not committed) |

The compile-check module is device-free: its IO ops are NULL, it asserts the
frame contract and the corrected address map at init, and it is not part of the
service module.  It exists to prove the library compiles and links under the
project's kernel build flags.

## Source hashes (committed text)

    4d3d4e4745a6e7ca0e9934e1ef9313c11711bfe9ecf4a3c791a8e97f0e2e6785  src/hcc_core.c        (1139 lines)
    511f8e1357573aea8fd3f08d44fe9913f0204326049a7be359253b54a348aa8f  src/hcc_core.h        (528 lines)
    e68b98587973436cafb810a1d3740261bafac063e47214e601f51b5d05263bbc  src/hcc_core_kernel.c (74 lines)
    79b8e75e9aa6dc0c3d462a678eb2a22cd1adf29b13266375cd45d793027d0107  src/hcc_core_kernel.h (45 lines)
    40b32ae5f585a810bc050cf28037105b75a3ad26cc93946846f85b8a0b53f195  src/kmod/hcc_core_check.c (59 lines)
    5b48aac71964e2e389a9d69330e42388a4e87908ace5d6bdb0ad4f470c6f224a  tests/hcc_core_test.c (1174 lines)
    74661be2e1a9a79fed60b4e09b83f6c438a47365cf3c373793085ee09dced246  tools/build.sh        (82 lines)

## Commands

    cd opensource/docs/incoming/wifi-hcc-core
    ./tools/build.sh                 # host build + selftest + kernel compile check
    ./tools/build.sh --no-kernel     # host build + selftest only

    # what the script runs for the host step:
    gcc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I src \
        src/hcc_core.c tests/hcc_core_test.c -o /tmp/hcc_core_test
    /tmp/hcc_core_test

Environment overrides: WORK (scratch dir, default /tmp/wifi-hcc-core-build),
CC (host compiler), KDIR, STAGING_DIR, CROSS_COMPILE.  If the kernel tree or
cross compiler is absent the script still passes the host selftest and prints
that the kernel check was skipped.

## API / portability notes handled

| issue | handling |
| --- | --- |
| kernel vs host basic types | the header selects hcc_u8/u16/u32/u64 + bool from linux/types.h (__KERNEL__) or stdint.h/stdbool.h (host); no libc beyond memset/memcpy/snprintf |
| kernel -Wdeclaration-after-statement style | all declarations are at block start in hcc_core.c |
| kernel -Wframe-larger-than (1024 B) | struct hcc_core is ~1.1 KiB, so the check module keeps it in a file-scope static; INTEGRATION.md tells the service to embed it (kmalloc'd or in ctx) rather than on the stack |
| CONFIG_MODULE_STRIPPED=y strips MODULE_DESCRIPTION | the check module uses MODULE_INFO(description,...) so modpost stays clean |
| host -Wall -Wextra -Werror -pedantic | the core compiles clean; the only casts are width-safe unsigned conversions |

## Not built / not claimed

- no OpenWrt image, no service module rebuild in this lane (the integration is
  performed by the merge lane; see INTEGRATION.md);
- no device interaction, no module load on the router, no verification against
  the vendor binary (the core is a decoded-contract reimplementation);
- the selftest is 100 % synthetic: it never reads a vendor artifact.
