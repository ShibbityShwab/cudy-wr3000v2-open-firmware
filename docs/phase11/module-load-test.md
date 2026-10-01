# The vendor kernel loads our modules (phase 11, 2026-10-01)

**Result: we can compile and load our own kernel modules on this device using only public Linux source
plus one header patch. Verified end to end on the router.**

## The test

- Source: vanilla `linux-5.10.201` from kernel.org, configured for ARM with the options that reproduce
  the vendor's module fingerprint.
- Patch: `arch/arm/include/asm/vermagic.h` is forced to emit no `p2v8` tag (the vendor kernel was built
  with `CONFIG_ARM_PATCH_PHYS_VIRT` off; a config toggle alone did not survive kconfig regeneration, a
  one-line header edit does).
- Build: GitHub Actions, `gcc-arm-linux-gnueabihf`, external module target. Artifact `hello.ko`.
- Module fingerprint produced: `5.10.201 SMP mod_unload ARMv7`
- Kernel's requirement (from the first, failing attempt): `5.10.201 SMP mod_unload ARMv7`

## The evidence, verbatim from the router

    insmod-rc=0
    1                     (lsmod shows the module)
    [37112.005804] omo-hello: loaded on the vendor 5.10.201 kernel
    rmmod-rc=0
    [37112.027415] omo-hello: unloaded

The first attempt failed with exactly one difference and the kernel said so itself:

    hello: version magic '5.10.201 SMP mod_unload ARMv7 p2v8 ' should be '5.10.201 SMP mod_unload ARMv7 '

## What this proves

1. The vendor kernel accepts modules built from public source. No vendor build tree is required for the
   module to *load*.
2. The module fingerprint is fully characterised and reproducible in CI.
3. Loading and unloading are clean; nothing on the device is disturbed.

## What it does not prove yet, stated plainly

- `printk` is one symbol. Real drivers need `ioremap`, PCI accessors, DMA and firmware-loading helpers;
  the vendor kernel is a full OpenWrt-era kernel and those subsystems are present, but each symbol has
  to be resolved from the module's own imports at build time and we have not yet linked against them.
- Kernel struct layouts may differ between our vanilla configuration and the vendor's. Modules that stay
  close to the metal (register windows, DMA buffers, the message ring) avoid most of that risk; deep
  integration with `net_device` or `cfg80211` would need care.
- The vendor's own driver still owns the radios while it is loaded. An open driver would either coexist
  for reading or take over the device deliberately, and that decision belongs to the device owner.

## Why it matters

The single blocker I described as "you cannot do this without Cudy's sources" is now half open: the
kernel will run our code. Everything the black-box work produced, the register windows, the 34-row
command table, the message envelope, the calibration store layout and its loaders, is exactly what a
module would use. The next milestone is a module that touches hardware instead of a log buffer.

## Reproduce

- Workflow: `.github/workflows/build-load-test-module.yml` (public repo, builds `lab/hello/hello.c`).
- Load test: `scp hello.ko root@192.168.10.1:/tmp/ && ssh root@192.168.10.1 'insmod /tmp/hello.ko; dmesg | tail -1; rmmod hello'`.
