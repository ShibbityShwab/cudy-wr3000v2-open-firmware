# BUILD-NOTES — hi3881 framework skeleton on Linux 6.18.60

**Result:** `hi3881_fw_skel.ko` (892,180 B, ELF32 ARM, license=GPL,
vermagic=`6.18.60 SMP mod_unload ARMv7 p2v8`), 28 objects / 307 text symbols, built with
the OpenWrt external toolchain (GCC 14.4.0) against the built kernel tree
`openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60`.

**Build line** (see `Kbuild` in this dir for the object list; the /home workdir copy lives at
`/home/shibbityshwab/wifi-framework-port-work/build/hi3881-618/`, not committed):

```
export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi
make ARCH=arm CROSS_COMPILE=$STAGING_DIR/bin/arm-openwrt-linux-muslgnueabi- CFG_BUILTIN=y \
     WIFI_DRIVER_DIR=$(pwd) -C $KDIR M=$(pwd) modules
```

**Source base:** themactep/wifi-hi3881 `driver/` (copied into the /home workdir), aligned
with the OpenHarmony hi3881v100 GPL-2.0 tree where the fork had diverged (typo fixes, missing
headers — see below). Only text changes; no upstream code is committed to the repo.

## Portability fix log (13 classes, all applied to the /home copy)

| # | file(s) | problem on 6.18 | fix |
| --- | --- | --- | --- |
| 1 | Kbuild | OpenWrt-patched kernel Makefile drops stock `-isystem $(CC -print-file-name=include)`; bare `<stddef.h>` (securec) not found | `ccflags-y += -isystem $(shell $(CC) -print-file-name=include)` |
| 2 | Kbuild | upstream trees rely on SDK-level `-I` flags | add `-I` for include/oal/oam/frw/mac/common/mac/hmac/hcc/wal/libsec |
| 3 | Kbuild | OS-selection macros not defined by any shipped config (themactep repo is missing `build/config/config.mk`) | `-D_PRE_OS_VERSION_LINUX=1 -D_PRE_OS_VERSION_LITEOS=2 -D_PRE_OS_VERSION=_PRE_OS_VERSION_LINUX` |
| 4 | Kbuild | multi-core/endian/frw-entity/feature macros undefined (same root cause) | full `_PRE_MULTI_CORE_MODE_*`, `_PRE_*_CPU_ENDIAN`, `_PRE_FRW_FEATURE_PROCESS_ENTITY_*`, `_PRE_WLAN_FEATURE_*` set mirroring OH env_config.mk |
| 5 | oal/oal_util.h | **vendor pins `LINUX_VERSION_CODE = kernel_version(3,10,0)`** — silently selects every oldest API branch (`wait_queue_t`, 3-arg `alloc_netdev`, `init_timer`…). `linux/version.h` no longer exists since 6.0 | use `<generated/uapi/linux/version.h>`; guard with `#ifndef` |
| 6 | include/hi_types_base.h | `typedef int intptr_t` clashes with kernel `linux/types.h`; object-like `MIN_T` clashes with OpenWrt-patched `linux/minmax.h` `MIN_T(type,a,b)` | guard both with `#ifndef __KERNEL__` |
| 7 | include/hi_stdlib.h | bare `extern size_t strlen(...)` etc. expand the fortified `strlen` macro (fortify-string.h) into a syntax error | guard the C-library declarations with `#ifndef __KERNEL__` |
| 8 | oal/oal_time.h | `getnstimeofday`/`do_gettimeofday`/`struct timespec`/`struct timeval` removed from kernel space (y2038 work) | `ktime_get_real_ts64` + `struct timespec64`; compat `struct timeval` in linuxkpi_compat.h (dmac_ext_if.h embeds it) |
| 9 | oal/oal_file.h | `mm_segment_t`/`get_fs()`/`set_fs(KERNEL_DS)` removed in 5.10 | drop them; `kernel_write()` needs no fs override |
| 10 | oal/oal_net.h | `netlink_kernel_cfg.cb_mutex` removed (4.15) | drop the line |
| 11 | oal/oal_timer.h | `del_timer`/`del_timer_sync` renamed to `timer_delete`/`timer_delete_sync` in 6.11 (old names not exported) | use the 6.11 names |
| 12 | oal/oal_thread.h | `sched_setscheduler` no longer exported; `struct sched_attr` needs `<uapi/linux/sched/types.h>` | `sched_setattr_nocheck()` + include |
| 13 | oal/oal_util.c | `random_ether_addr` not exported | inline `eth_random_addr()` (+ `<linux/etherdevice.h>`) |

Additional tree-alignment fixes: frw_task.c `PROCCESS`→`PROCESS` typo (the OpenHarmony
tree has the correct spelling), `hi_wifi_api.h`/`hi_wifi_csi_api.h` copied from the OH tree
(ta fork lacks them), `oal_frw_exception_report` stubbed in skel_mod.c because
`oal/exception_rst.c` is HDF-bound (`hdf_ibus_intf.h` lives in OpenHarmony's driver
framework, not in either tree).

## What is NOT yet compilable (documented, not attempted in this lane)

- **wal/** — cfg80211 surface: `wiphy_new`→`wiphy_new_nm`, cfg80211_ops field renumbering,
  netdev/skb layout drift. This is the known M3 blocker set from `wifi-shim-port`; at source
  level each is a mechanical rewrite, unlike the binary shim.
- **hcc/** — the hi3881 hcc_host is **SDIO**-oriented (ADMA descriptors, hcc_slave); the hi5622
  host side is PCIe mailbox + ETE rings (`pcie_msg_*`/`hcc_msg_*`). Swap per
  `wifi-driver-re` §3-4, not per hi3881.
- **dpe/** (hi1105 data path) — parallel to hi5622's `hdpp_*`; names differ, semantics similar.
- The full hmac_/wal_ ko-only delta (1,503 + 684 functions) — no public source.

## License of the artifacts in this directory

`linuxkpi_compat.h` and `skel_mod.c` were written by this lane (GPL-2.0, matching the
GPL-2.0 hi3881 driver files they compile with). `Kbuild` is build metadata. No upstream-tree
source text is committed anywhere in this repo.
