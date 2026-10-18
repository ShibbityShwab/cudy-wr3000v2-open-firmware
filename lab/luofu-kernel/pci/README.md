# pci/ - luofu PCIe host driver

Published 2026-10-18 into the luofu-kernel tree.  The source was not invented:
it is the existing lab driver, copied and compile-gated.  This tree lives at
`opensource/lab/luofu-kernel/` inside the `opensource` repository in this
checkout.

## Provenance

| item | value |
| --- | --- |
| source | `opensource/lab/luofu-pcie/luofu-pcie.c` |
| source git blob (HEAD) | `eb2dc723b2781eea083b9dc220c8914c51b70884` |
| last commit touching it | `6d09fe652b6ac4609ca787eadfcca014aba871e9` (2026-10-16) |
| sha256 of the source bytes (before this delta) | `5629b274e8110e9b47909f3ddb0eac4467641979c6639ae3cba93b6a67511cd4` |
| size | 71,119 bytes |
| authoritative notes | `pcie-bringup-written.md`, `pcie-forced-probe.md` (memory repo, `repo/cudy-wr3000v2/`) |

The driver itself names its spec source: `build/tmp/inta-spec/pcierc.md`
(register receipt `pciskel.md`), distilled from the vendor `hi_pcie.ko`.

## Delta from the source (and only this)

1. The provenance header at the top of `luofu-pcie.c`.
2. A compile gate, `CONFIG_LUOFU_PCIE_ENABLE_LINK_WRITES` (`pci/Kconfig`,
   **default n**), around every store that programs the RC:
   - `luofu_pcie_write()`, the aligned-dword read-modify-write accessor;
   - the whole bring-up: `luofu_rc_stage()`, `luofu_pcie_crg_handshake()`,
     `luofu_pcie_rc_mode()`, `luofu_pcie_wait_for_link()`,
     `luofu_pcie_bringup()`;
   - the `rc_write_test`/`rc_write_flip` CRG instrument and its store
     helpers (`luofu_rc_store`, `luofu_rc_rmw`, `luofu_rc_write_same`).
   The pre-existing `LUOFU_RC_WRITE` switch is now
   `#define LUOFU_RC_WRITE LUOFU_PCIE_LINK_WRITES`, so one symbol controls
   both; a `-DLUOFU_RC_WRITE=0/1` on the build line no longer overrides it.
3. With the config off, `luofu_pcie_bringup()` is a warning-only stub that
   returns `-EOPNOTSUPP`, and the two runtime warning strings name the config
   instead of the old macro.
4. `luofu_pcie_dwc_link_up()` is marked `__maybe_unused`: only the gated
   bring-up calls it, so the default build would otherwise emit an
   unused-function warning.

No register, offset, bit, value, read path or probe behaviour was changed.
With the default config, the module compiles no `writel()` at all and the
probe remains the read-only inventory it was.

## What the gate covers

| path | stores | source function |
| --- | --- | --- |
| misc RC mode | `writel(0x40000000, misc+0x00)` | `luofu_pcie_rc_mode()` |
| misc app control | `writel(iATU-en bit 13 \| LTSSM-en bit 11, misc+0x1c)`, twice | `luofu_pcie_rc_mode()`, `luofu_pcie_bringup()` |
| DBI command | aligned RMW, `dbi+0x004 = 0x7` | `luofu_pcie_bringup()` |
| DBI Link Control | aligned RMW, `dbi+0x080` ASPM = `0x3` | `luofu_pcie_bringup()` |
| CRG handshake | `pcie_clk` enable + four resets deasserted apb -> pcs -> phy -> ctrl, 50 us apart | `luofu_pcie_crg_handshake()` |
| stage code | `writel(0xC0DE70xx, crg+0x3c)` | `luofu_rc_stage()` |
| CRG instrument | bounded RMW of reset word `0x34` / gate word `0x20` (5-store budget) | `luofu_pcie_rc_write_test()`, `luofu_pcie_rc_write_flip()` |

**IATU scope, stated precisely:** the compiled bring-up enables the iATU
through the misc `iATU-en` bit.  The per-viewport iATU table writes
(`DBI+0x900+0x200*i`) exist only in the commented `hi_pcie_probe` scaffold
(source lines 1441-1451) and are not compiled anywhere.  Any future port of
that loop must sit behind the same config.

## Why the default is off (measured, not theoretical)

- `pcie-forced-probe.md` (2026-10-06/07): our read-only module read the live
  RC's DBI (link UP, `DL_ACTIVE=1`, 0 writes), then **panicked the box** on a
  read of the 2-byte Link Status at `dbi+0x082`.  The first ko did
  `readl(dbi+0x082)`; a second ko tried `readw(dbi+0x082)` and also aborted
  (`ldrh r4,[r3]` at the same 2-mod-4 offset).  The DBI/CFG windows answer
  only 4-byte-aligned 32-bit accesses, whatever the width.  A write path of
  sub-word or unaligned stores is the same fault class, so the gated write
  accessor is an aligned-dword read-modify-write.
- `pcie-bringup-written.md` (2026-10-15): the bring-up is real code following
  the vendor's 14-step `hi_pcie_probe` order (clock, four resets, RC mode,
  iATU, LTSSM, link poll) and it built cleanly as 21,864 bytes, but it was
  never validated against a device.  The misc window it writes is
  **write-only host-side** - a read of it panicked a box - so the stores
  cannot be checked by read-back.
- The driver's own `luofu_pcie_rc_write_flip` comment (source lines
  1151-1157, citing `rcwrite.md` sec 2b) records the other measured hazard: a
  state-changing CRG store on a LIVE RC wedges the DWC link and drops the
  wiphy; only a watchdog/reboot recovers.

A plain `insmod luofu-pcie.ko` must therefore not be able to program the PCIe
link.  Enabling the option is a deliberate, serial, health-gated experiment,
not a compile-time convenience.

## Not yet a working host controller

The driver is a read-only frame plus a gated bring-up.  `probe()` returns 0
without calling `pci_host_probe()`; `luofu_pcie_remove()` has no root-bus
teardown; the endpoint power-up (GPIO) and linkdown IRQ are still scaffold
comments.  Publishing this file does not assert that the RC is brought up.

## Build

Add the two symbols from `pci/Kconfig` to
`drivers/pci/controller/Kconfig` (or source this file from wherever you
install it), and add to `drivers/pci/controller/Makefile`:

```
obj-$(CONFIG_LUOFU_PCIE) += luofu-pcie.o
```

Standalone module check (the lane used by the lab):

```sh
make -C /opt/kbuild/linux-5.10.201 M=$PWD/lab/luofu-kernel/pci \
     ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
```

To compile the bring-up for a device experiment, add the symbol to the kernel
`.config` (`CONFIG_LUOFU_PCIE_ENABLE_LINK_WRITES=y`) or, for the standalone
module check only, `ccflags-y += -DCONFIG_LUOFU_PCIE_ENABLE_LINK_WRITES=1`.
The runtime knobs are still required: `force_probe=1 rc_bringup=1` for the
bring-up, and `force_probe=1 rc_write_test=1` (link UP, no-op census) or
`rc_write_flip=1` (link DOWN, quiesced) for the CRG instrument.

## Verification performed when publishing

- `grep -c LUOFU_PCIE_ENABLE_LINK_WRITES pci/luofu-pcie.c` -> 7
- `grep -n 'default n' pci/Kconfig` -> both symbols (driver and gate)
- compiled both branches out-of-tree from a `/tmp` copy: config off and
  config on, cross-compiled for arm against the prepared 5.10.201 tree, zero
  warnings on either.  The gate-off `.ko` is 16,120 bytes and carries the
  "bring-up is compiled out" warning string but no "RC bring-up complete"
  string; the gate-on `.ko` is 21,864 bytes and is the reverse.  The gate-on
  size matches the 21,864-byte build recorded in `pcie-bringup-written.md`.
- no device contact was made in this phase.
