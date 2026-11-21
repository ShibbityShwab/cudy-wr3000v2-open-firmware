# wifi_shim - 5.10->6.18 compatibility shim (DRAFT)

Design draft for loading the vendor Hi5622V100 5.10.201 Wi-Fi binaries on our
6.18.60 OpenWrt luofu image. Host-produced; **never bench-loaded as-is** - the
M3 struct-layout audit returned NO-GO before bench compilation was needed (see
../SUMMARY.md). The draft compiles against the 6.18 built tree (COMPILE-OK,
raw/wifi_shim.o, cross gcc 14.4.0).

## What it does

Exports the 5.10-named kernel symbols our 6.18 vmlinux no longer exports
(16 symbols + 2 hi_pcie aux stubs), each mapped to its 6.18 equivalent:

| 5.10 name | 6.18 mapping | note |
| --- | --- | --- |
| printk | vprintk() | not exported on 6.18 (only _printk/_dev_printk) |
| __kmalloc / kmem_cache_alloc_trace | *_noprof() | CONFIG_MEM_ALLOC_PROFILING renames |
| kmalloc_order_trace | alloc_pages_exact_noprof() | order pages; trace lost |
| vmalloc | vmalloc_noprof() | |
| alloc_workqueue | alloc_workqueue_noprof() | fmt pre-expanded (no __alloc_workqueue export) |
| kfree_skb / consume_skb | __kfree_skb() | 6.18 static inlines; no drop accounting |
| __dev_kfree_skb_any | dev_kfree_skb_any_reason(NOT_SPECIFIED) | |
| skb_queue_purge | skb_queue_purge_reason(NOT_SPECIFIED) | |
| hrtimer_init | hrtimer_setup(+stub cb) | 6.13 rework; see M3 hrtimer drift note |
| kern_path_create / done_path_create | kern_path()+lookup_one() / path_put()+dput() | reconstructed pair |
| dma_sync_single_for_{cpu,device} | 6.18 inline body replicated | |
| mem_section | FLATMEM stand-in array | DANGEROUS: see below |
| wireless_send_event | no-op | WEXT core absent on 6.18 |
| PDE_DATA | pde_data() | for hi_pcie.ko |
| of_get_named_gpio_flags | -ENOSYS stub | for hi_pcie.ko; see bench notes |

## What it does NOT provide (by design)

Vendor hi_* platform symbols come from the other vendor .ko binaries (M1
closure, depth<=3). Insmod order on the bench (all vermagic-patched LOCAL
copies, tools/vermagic-patch.py):

    hi_basic.ko  ->  hi_kpie.ko  ->  ksecurec.ko  ->  compat.ko  ->  cfg80211.ko
    ->  hi_kwificlk.ko*  ->  hi_pcie.ko  ->  wifi_shim.ko  ->  hi5622v100_plat.ko
    ->  hi5622v100_wifi.ko

(* hi_kwificlk.ko is referenced by the stock wifi_init.sh but is NOT present in
   recovery-extract; obtain from the device rootfs or skip if the clock is up.)

Provider edges (machine-derived): wifi.ko <- plat.ko (138 symbols, the oal_/oam_/
hcc_/dmac_ platform API), wifi.ko <- cfg80211.ko (26 symbols, the whole 5.10
cfg80211 API incl. wiphy_new_nm/wiphy_register), both <- ksecurec.ko (11
securec_*), both <- hi_kpie.ko (2). No wifi.ko import needs mac80211 (fullmac).

## Known non-viability (why this is a DRAFT)

The import layer is shimmable; the STRUCT-LAYOUT layer is not. Verified baked
5.10 offsets in the binaries that a shim cannot redirect (M3b/M3d):

- driver reads netdev->state @0x28 (6.18: 0x88) in ndo_set_mac_address;
  writes netdev->mtu @0x148 (6.18: 0x30) in the mtu-change path;
- driver + cfg80211.ko read skb->head/tail/end/mac_header/users/mark at 5.10
  offsets (+8 drift on 6.18) in RX/TX paths;
- cfg80211.ko nl80211 doit handlers write genl_info.genlhdr @0x0c (6.18:
  inside the nlhdr pointer) - the first nl80211 command corrupts memory;
- hrtimer_init's 5.10 hrtimer layout differs from 6.18 (function/state fields);
- mem_section: vendor kernel was SPARSEMEM, ours is FLATMEM - the stand-in
  array above is unverified guesswork for plat.ko's oal_dma_map_single.

## Regeneration

    K=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    GCC=.../arm-openwrt-linux-muslgnueabi-gcc
    $GCC -c -o wifi_shim.o wifi_shim.c -D__KERNEL__ -DKBUILD_MODNAME='"wifi_shim"'       -D__LINUX_ARM_ARCH__=7 -DMODULE -include $K/include/generated/autoconf.h       -I$K/include -I$K/arch/arm/include -I$K/include/uapi -I$K/arch/arm/include/uapi       -I$K/arch/arm/include/generated -I$K/arch/arm/include/generated/uapi -w
