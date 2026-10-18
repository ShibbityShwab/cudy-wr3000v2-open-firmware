# fragment-audit - CONFIG_*LUOFU* in the built `.config` vs the files they build

Audit date 2026-10-18. Device-free: nothing was run against the router.
Tree audited: `/opt/kbuild/linux-5.10.201` (WSL Ubuntu, root), `.config` 97,185 B, mtime 2026-10-16 14:46:44 +0700.
Fragment read: `lab/luofu-kernel/luofu.fragment` (482 lines incl. trailing newline).

## Enumeration command (run verbatim in WSL, output pasted)

```
$ cd /opt/kbuild/linux-5.10.201
$ grep -o '^CONFIG_[A-Z0-9_]*LUOFU[A-Z0-9_]*=[ymn]' .config
CONFIG_ARCH_LUOFU=y
CONFIG_MTD_NAND_LUOFU=y
CONFIG_LUOFU_GMAC=y
CONFIG_LUOFU_MDIO=y
CONFIG_LUOFU_WATCHDOG=y
CONFIG_LUOFU_CLK=y
$ grep -c '^CONFIG_[A-Z0-9_]*LUOFU[A-Z0-9_]*=[ymn]' .config
6
$ grep -n LUOFU .config
281:CONFIG_ARCH_LUOFU=y
1180:CONFIG_MTD_NAND_LUOFU=y
1451:CONFIG_LUOFU_GMAC=y
1452:CONFIG_LUOFU_MDIO=y
1898:CONFIG_LUOFU_WATCHDOG=y
2942:CONFIG_LUOFU_CLK=y
```

## Table

"Built file" = the `.c` the symbol's own Kconfig entry plus Makefile wiring produces (the Makefile line that
names the symbol is quoted per row). "Exists" = `test -e` on that path in the build tree, cross-checked
against the compiled object.

| Symbol | Value | Kconfig entry | Makefile line that builds it | File | Exists |
|---|---|---|---|---|---|
| `CONFIG_ARCH_LUOFU` | =y | `arch/arm/mach-hisi/Kconfig:58` - `bool`, `depends on ARCH_MULTI_V7`; help text names the machine code **`arch/arm/mach-luofu/luofu.c`** | `arch/arm/Makefile:185` `machine-$(CONFIG_ARCH_LUOFU) += luofu` -> machine dir `arch/arm/mach-luofu/`, whose `Makefile` has `obj-y += luofu.o` | `arch/arm/mach-luofu/luofu.c` | **YES** (28,173 B; `luofu.o` in `built-in.a`) |
| `CONFIG_MTD_NAND_LUOFU` | =y | `drivers/mtd/nand/luofu/Kconfig:11` - `depends on ARCH_LUOFU \|\| COMPILE_TEST` | `drivers/mtd/nand/Makefile:11` `obj-$(CONFIG_MTD_NAND_LUOFU) += luofu/` -> `drivers/mtd/nand/luofu/Makefile` `obj-$(CONFIG_MTD_NAND_LUOFU) += luofu-fmc.o` | `drivers/mtd/nand/luofu/luofu-fmc.c` | **YES** (230,172 B; `luofu-fmc.o` in `built-in.a`) |
| `CONFIG_LUOFU_GMAC` | =y | `drivers/net/luofu/Kconfig:1` - `depends on ARCH_LUOFU` | `drivers/net/Makefile:87` `obj-$(CONFIG_LUOFU_GMAC) += luofu/` -> `drivers/net/luofu/Makefile` `obj-$(CONFIG_LUOFU_GMAC) += luofu-gmac.o` | `drivers/net/luofu/luofu-gmac.c` | **YES** (25,474 B; `luofu-gmac.o` built) |
| `CONFIG_LUOFU_MDIO` | =y | `drivers/net/luofu/Kconfig:7` - `depends on ARCH_LUOFU` | `drivers/net/luofu/Makefile:3` `obj-$(CONFIG_LUOFU_MDIO) += luofu-mdio.o` (no entry in `drivers/net/Makefile`; reached via the `luofu/` dir that GMAC =y enables) | `drivers/net/luofu/luofu-mdio.c` | **YES** (16,311 B; `luofu-mdio.o` built) |
| `CONFIG_LUOFU_WATCHDOG` | =y | `drivers/watchdog/Kconfig:2168` - `tristate`, `depends on ARCH_LUOFU \|\| COMPILE_TEST` | `drivers/watchdog/Makefile:230` `obj-$(CONFIG_LUOFU_WATCHDOG) += luofu-wdt.o` | `drivers/watchdog/luofu-wdt.c` | **YES** (8,841 B; `luofu-wdt.o` built) |
| `CONFIG_LUOFU_CLK` | =y | `drivers/clk/luofu/Kconfig:1` - `depends on ARCH_LUOFU` | `drivers/clk/Makefile:127` `obj-$(CONFIG_LUOFU_CLK) += luofu/` -> `drivers/clk/luofu/Makefile` `obj-$(CONFIG_LUOFU_CLK) += luofu-clk.o` | `drivers/clk/luofu/luofu-clk.c` | **YES** (58,287 B; `luofu-clk.o` built) |

Build evidence: `vmlinux` present in `/opt/kbuild/linux-5.10.201` (11,195,616 B, 2026-10-18 21:16);
each row's `.o` exists next to its source and appears in the directory's `built-in.a`.

## Count line

```
COUNT: 6 symbols in .config; file exists 6; file missing 0
ENUM:  grep -c '^CONFIG_[A-Z0-9_]*LUOFU[A-Z0-9_]*=[ymn]' .config  ->  6   (matches the 6 rows above)
```

**6 of 6 `CONFIG_*LUOFU*` symbols (including `CONFIG_MTD_NAND_LUOFU`) point at a file that exists;
0 point at a missing file.**

## The record's "12 driver-less CONFIG_*_LUOFU remain" - corrected

For the built `.config`: **0** driver-less. It holds six LUOFU symbols and the file behind each one exists.
"12" cannot be a `.config` count - it is larger than the whole population.

The number is best explained by a fragment/lab count, not by `.config` - but it does not hold there either:

- The fragment requests 13 `CONFIG_*LUOFU*` symbols: the 6 above plus `CONFIG_PINCTRL_LUOFU`,
  `CONFIG_PCIE_LUOFU`, `CONFIG_LUOFU_TVSENSOR`, `CONFIG_LUOFU_HWRNG`, `CONFIG_LUOFU_EFUSE`,
  `CONFIG_LUOFU_LSW`, `CONFIG_LUOFU_WIFI`. Exactly **7** of those 13 are absent from `.config` (verified by
  `grep -n LUOFU .config`: none of them appears, not even as `is not set`).
- The fragment's spelling `CONFIG_CLK_LUOFU` is not the build tree's `CONFIG_LUOFU_CLK` (Kconfig
  `drivers/clk/luofu/Kconfig:1`); same driver, but on a fresh merge the fragment line misses the symbol -
  `.config` got `LUOFU_CLK` from the tree's own Kconfig.
- Of those 7, four (`PINCTRL_LUOFU`, `PCIE_LUOFU`, `TVSENSOR`, `WIFI`) have source in the public tree
  `opensource/lab/`: `luofu-pinctrl/luofu-pinctrl.c`, `luofu-pcie/`, `luofu-thermal/luofu-thermal.c`,
  `luofu-wifi/luofu-wifi.c` (all four directories exist). `TVSENSOR`'s lab dir is named `luofu-thermal`
  (the old Makefile drift, per the memory note `luofu-bringup-lane.md`).
- The other three (`HWRNG`, `EFUSE`, `LSW`) have no source in the lab tree, and no Kconfig entry in the
  build tree: `grep -rn LUOFU --include=Kconfig* /opt/kbuild/linux-5.10.201` returns only the six audited
  symbols. The fragment itself calls `CONFIG_LUOFU_WIFI` "a placeholder for now".
- Defensible statement: **7 requested LUOFU symbols never reached `.config`; of those, 3 have neither a
  Kconfig entry nor a source file in either tree.** Neither figure is 12.

## Caveats

- The context said the published submodule holds only `mach-luofu/luofu.c` etc.; its actual file list also
  has `mach-luofu/Makefile`, `nand/luofu/Kconfig`, `nand/luofu/Makefile`, `nand/triductor/*.o` + Makefiles,
  `nand/parsers/ofpart_triductor.o`, `luofu-shrink.fragment`, `patched-compressed-head.S`,
  `patched-head-common.S`.
- `MTD_NAND_LUOFU`'s entry in the build tree (`drivers/mtd/nand/luofu/Kconfig`, 2,268 B) is a host-side
  copy, not the submodule's `nand/luofu/Kconfig`; both exist, and the existence test above is against the
  build copy.
- `lab/luofu-kernel/` also shows untracked `clk/`, `pci/`, `pinctrl/`, `dts/` directories dated 21:42-21:45 on
  2026-10-18 - they appeared while this audit ran and belong to another concurrent lane. They were only
  listed (`find`/`ls`/`git status`), never read or touched, and they are not part of `.config`. The build tree
  root also holds pre-existing `luofu-uImage` / `luofu-control-uImage`.

## Appendix - the Makefile lookup behind the table (output pasted, run in WSL)

```
$ cd /opt/kbuild/linux-5.10.201
$ grep -Fn "(CONFIG_" drivers/clk/Makefile drivers/clk/luofu/Makefile drivers/net/Makefile \
      drivers/net/luofu/Makefile drivers/mtd/nand/Makefile drivers/mtd/nand/luofu/Makefile \
      drivers/watchdog/Makefile | grep -F '+=' 
drivers/clk/Makefile:127:obj-$(CONFIG_LUOFU_CLK) += luofu/
drivers/clk/luofu/Makefile:2:obj-$(CONFIG_LUOFU_CLK)	+= luofu-clk.o
drivers/net/Makefile:87:obj-$(CONFIG_LUOFU_GMAC) += luofu/
drivers/net/luofu/Makefile:2:obj-$(CONFIG_LUOFU_GMAC)	+= luofu-gmac.o
drivers/net/luofu/Makefile:3:obj-$(CONFIG_LUOFU_MDIO)	+= luofu-mdio.o
drivers/mtd/nand/Makefile:11:obj-$(CONFIG_MTD_NAND_LUOFU) += luofu/
drivers/mtd/nand/luofu/Makefile:2:obj-$(CONFIG_MTD_NAND_LUOFU)	+= luofu-fmc.o
drivers/watchdog/Makefile:230:obj-$(CONFIG_LUOFU_WATCHDOG) += luofu-wdt.o
$ grep -n "CONFIG_ARCH_LUOFU" arch/arm/Makefile
185:machine-$(CONFIG_ARCH_LUOFU)		+= luofu
$ ls -l arch/arm/mach-luofu/luofu.c drivers/mtd/nand/luofu/luofu-fmc.c \
      drivers/net/luofu/luofu-gmac.c drivers/net/luofu/luofu-mdio.c \
      drivers/watchdog/luofu-wdt.c drivers/clk/luofu/luofu-clk.c
-rwxr-xr-x 1 root root  28173 Oct 15 15:51 arch/arm/mach-luofu/luofu.c
-rwxr-xr-x 1 root root  58287 Oct 14 19:30 drivers/clk/luofu/luofu-clk.c
-rwxr-xr-x 1 root root 230172 Oct 18 21:16 drivers/mtd/nand/luofu/luofu-fmc.c
-rwxr-xr-x 1 root root  25474 Oct 14 19:30 drivers/net/luofu/luofu-gmac.c
-rwxr-xr-x 1 root root  16311 Oct 14 19:30 drivers/net/luofu/luofu-mdio.c
-rwxr-xr-x 1 root root   8841 Oct 16 01:10 drivers/watchdog/luofu-wdt.c
$ ls -l vmlinux
-rwxr-xr-x 1 root root 11195616 Oct 18 21:16 vmlinux
```

The command above was run with the eight explicit Makefiles (a tree-wide `grep ... --include=Makefile`
also matches every `obj-$(CONFIG_...)` line in the tree and is unreadable); the eight lines it needs for the
symbols in `.config` are the ones plus the `machine-` line shown, and each target was then expanded to
`dir/<obj>.c` and tested with `ls` in the same session.

## Scope

Read: `luofu.fragment`, `/opt/kbuild/linux-5.10.201/.config`, the Kconfigs/Makefiles listed above,
directory listings under `arch/arm/` and `drivers/{clk,net,mtd/nand,watchdog}/`. Written: this file only
(scratch under `/tmp` and `%TEMP%`). No device contact; no lasting change in
`/opt/kbuild/linux-5.10.201` (the one scratch file an early attempt wrote there, `.luofu-makefiles.txt`,
was deleted the same minute); no `.omo-secrets/` access; no register values.
