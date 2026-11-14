# Running the vendor's driver stack on a mainline-ish kernel

**Status: working, 2026-11-14.** The vendor's switch, MDIO, PHY, MAC and `hi_swa` modules load and run
on a vanilla 5.10.201 ARM kernel. On the bench box this produces a fully working router: `eth2` up with
its Hisilicon PHY, `br-lan` at 192.168.10.1, WiFi vaps bridged, `ping` 3/3.

This document is the complete recipe, including the two non-obvious blockers and why the obvious fixes
fail. It is written so the whole thing can be reproduced from a stock vendor image.

---

## Why the modules do not load on a stock kernel

```
hi_kpinctrl: module PLT section(s) missing
```

Every one of the vendor's ~60 modules was built for a kernel with `CONFIG_ARM_MODULE_PLTS=n`. The
vendor configured `-mlong-calls` and no PLT support, so their modules contain no `.plt` or `.init.plt`
section. A stock multi_v7 kernel enables `CONFIG_ARM_MODULE_PLTS`, and
`arch/arm/kernel/module.c:module_frob_arch_sections()` hard-rejects any module that lacks both:

```c
if (!(mod->arch.plt && mod->arch.init_plt)) {
        pr_err("%s: module PLT section(s) missing\n", mod->name);
        return -ENOEXEC;
}
```

### The fix that works: add the sections, never use them

`objcopy` can append zero-size sections to an existing `.ko` without touching code or relocations:

```sh
: > /tmp/empty.bin
arm-none-linux-gnueabihf-objcopy \
    --add-section .plt=/tmp/empty.bin \
    --add-section .init.plt=/tmp/empty.bin \
    hi_kmdio.ko
```

The loader is then satisfied: the sections exist and are empty, so the PLT allocation loop has nothing
to allocate. This is only safe if the module never needs a PLT entry. That requires the module to be
loaded within the 32 MB branch range of the kernel text, which a stock `module_alloc` does not
guarantee. Two options:

1. Build the kernel with `CONFIG_ARM_MODULE_PLTS=n` (the vendor's own configuration). The loader still
   runs `module_frob_arch_sections`, so the zero-size sections are still required, but nothing ever
   looks for a PLT entry.
2. Keep PLTs enabled and constrain `module_alloc` to the near region (`MODULES_VADDR`..`MODULES_END`),
   so `get_module_plt()` is unreachable. This is what our lab kernel does, with a boot print
   `luofu: module_alloc N bytes -> bf000000 (near region only)` as evidence.

Do **not** try to teach `module_frob_arch_sections` to tolerate missing sections. We tried: patching
the check and guarding `get_module_plt` broke the boot 5/5 while the controls booted 3/3, because the
relocation path reaches the PLT allocator through a second route. The objcopy approach leaves the
kernel untouched and is what ships.

## Why `finit_module` returns EINVAL even with the sections present

If you load the modules from your own init or tool rather than the vendor's `insmod`, check the syscall
number. On ARM, in `arch/arm/tools/syscall.tbl`:

```
379  common  finit_module  sys_finit_module
```

350 is the x86_64 number. Calling 350 on ARM invokes an unrelated syscall with `(fd, "", 0)`, which
returns `-EINVAL` before the module is ever read. The symptom is a load log where every module reports
`fd=0 ret=-22` and `dmesg` shows nothing at all.

## The load order

The vendor's order is the dependency graph, and it lives in `/etc/rc.d/luofu`:

```
hi_basic
hi_kpinctrl -> (gpio) -> (rng) -> hi_efuse -> hi_kpmupwm -> hi_clk_corrector -> hi_cpufreq
hi_sysctrl -> hi_acp -> hi_kpie
hi_lsw_pub -> hi_lsw_dp -> hi_lsw_pfe          # switch first
hi_kmdio -> hi_kphy -> hi_kmac                 # MDIO, PHYs, MACs
hi_kteam                                      # board-specific, absent on R116
hi_kcfe_pub -> hi_kcfe_lrn(+l3/l4/mc/mc_list/vxlan) -> hi_kcara -> hi_khslink
hi_swa                                        # switchdev glue, needs all of the above
```

`hi_swa` tolerates none of this being absent: it fails with `Unknown symbol hi_cfe_flush_by_mac`,
`hi_net_get_port`, `hi_cfe_add_subif`, `ppp_get_relate_dev`, `memcpy_s`, `sscanf_s` and a dozen more
if the base modules are not already resident.

The initramfs init loads this exact sequence by `finit_module` from `/vendor/upper/lib/hisilicon/ko/`
(the patched copies in the overlay upper) before the pivot, so the vendor's own scripts later see the
modules as already present. Source: `lab/vendor-stack/init-vendorroot.c` in this repository.

## What you should see when it works

```
Hi-GEMAC 14480000.gemac eth2: PHY [14000000.mdio0:04] driver [Hisilicon Genius Phy] (irq=POLL)
Hi-GEMAC 14480000.gemac eth2: configuring for phy/gmii link mode
br-lan: port 1(eth2) entered blocking state
device eth2 entered promiscuous mode
[DFX]ETH eth2 phyid:4 Starting TDR...
```

and then `eth2` UP with `LOWER_UP`, `br-lan` with `192.168.10.1/24`, and the WiFi vaps bridged in.
