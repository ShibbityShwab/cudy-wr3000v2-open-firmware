# luofu-clk

CRG clock + reset controller **driver structure** for the Hi5671Y "luofu" SoC,
matching the `hisilicon,luofu-crg` node in `opensource/docs/soc/luofu-r116.dts`.
It carries the platform-driver shape plus a real clock tree: the pinned geometry
(16 gates + 2 muxes + 2 PLLs) is transcribed into structs, wired through the
generic `clk-provider.h` API with our own `clk_ops`, and exposed as a DT onecell
provider so `clocks = <&crg LUOFU_CLK_*>` resolves by index.

The module **compiles** in the CI cross-build against the vanilla 5.10.201 arm
headers via `.github/workflows/lab-module-build.yml` (target `lab/luofu-clk`,
triggered on `omo/**`); the resulting `luofu-clk.ko` is uploaded as the
`luofu-clk-ko` artifact. The CI also runs a rule-census step over the source
(see below).

## What is real (evolved from the stage-1 skeleton)

- **The CRG model** (`luofu-clk.c`): `luofu_gates[]` (16 gates), `luofu_muxes[]`
  (2 muxes), `luofu_plls[]` (2 PLLs), each with its pinned `reg-offset`/`bit`,
  output name, and parent (transcribed from the pinned `clocks` cells).
- **The driver ops**: `luofu_gate_ops` (enable/disable/is_enabled through the
  proven dword-aligned one-bit RMW), `luofu_pll_ops` (read-only `recalc_rate` +
  status-bit `is_enabled`), `luofu_mux_ops` (read-only `get_parent`;
  `set_parent` refuses `-EPERM` because `0x138` is out of bounds for writes).
- **DT registration**: on the DT path `luofu_register_clks()` registers PLLs ->
  muxes -> gates and adds the onecell provider; every clock is tagged
  `CLK_IGNORE_UNUSED` so `clk_disable_unused()` cannot kill a U-Boot-enabled
  gate.
- **The binding note**: `bindings/hisilicon,luofu-crg.md`.

## What stays (the forced-probe smoke path, unchanged)

`force_probe=1` still synthesizes a name-matched platform device (no `of_node`)
and runs the read-only status inventory; the bounded write instruments
`write_test=1` / `write_flip=1` / `rst_flip=1` are untouched and remain the
only store sources, all 0-default. A bare `insmod` stays read-only forever.

## Still TODO (the reset provider)

The reset half (`#reset-cells = <2>`, `resets = <&crg off bit>`) is not yet
registered; the `softrst_val0/1` magic belongs there. See
`build/tmp/inta-spec/drvcrg.md` for what evolved vs. what remains.

## Build + CI

```bash
# cross-build (same lane CI runs):
make -C <linux-5.10.201> M="$PWD" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
```

CI (`lab-module-build.yml`) builds the module and runs a **CRG rule census**
that fails the build if the source violates the access rules: a `writel` outside
the single RMW primitive, the forbidden addresses `0x400392f0` / `0x10161000` /
`0x4016010c`, or a sub-word/misaligned store shape.
