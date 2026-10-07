# Hisilicon Hi5671Y "luofu" CRG binding (`hisilicon,luofu-crg`)

Binding note for the lab CRG driver (`opensource/lab/luofu-clk/luofu-clk.c`).
This is a working transcription of the pinned vendor tree
(`opensource/docs/soc/luofu-r116-pinned.dts`) into the single-CRG clock+reset
controller shape cloned from `drivers/clk/hisilicon/crg-hi3798cv200.c`
(ref: `build/tmp/inta-spec/clocks2.md`). A formal `.yaml` binding lands with the
in-tree driver; this note pins the geometry the lab `.ko` builds against.

## Node

```dts
crg: clock-reset-controller@14880000 {
	compatible = "hisilicon,luofu-crg", "syscon", "simple-mfd";
	reg = <0x14880000 0x1000>;	/* pinned clk@14880000 reg :205 */
	#clock-cells = <1>;		/* LUOFU_CLK_* index namespace */
	#reset-cells = <2>;		/* (reg-offset, bit), pinned reset0 :1395 */
};
```

- `#clock-cells = <1>`: consumer `clocks = <&crg LUOFU_CLK_X>` resolves by the
  index table below through `of_clk_hw_onecell_get`.
- `#reset-cells = <2>`: consumer `resets = <&crg off bit>`; `off` is the CRG
  page register offset, `bit` the reset bit. Polarity (pinned `hsan,hsan-reset`):
  `1` = deasserted / out of reset (`hi_reset_deassert` SETs the bit), `0` =
  asserted / in reset (`hi_reset_assert` CLEARs it).
- `"syscon", "simple-mfd"` are kept so later blocks (reboot/watchdog children)
  can `syscon_regmap_lookup_by_phandle` the page without a second mapping.

## Fixed input clocks (parents, already free mainline bindings)

| phandle | name | frequency | pinned |
|---|---|---|---|
| `0x3` | `osc` | 20 MHz | `:756` |
| `0x6` | `apb_clk` | 100 MHz | `:187` |
| `0x7` | `ahb_clk` | 200 MHz | `:173` |
| `0x8` | `gemacN_clk` | 125 MHz | `:955` |
| `0x9` | `lsw_dp_clk` | 166 MHz | `:1096` |
| `0xa` | `lsw_pfe_clk` | 333 MHz | `:1114` |
| `0xb` | `efuse_ring_clk` | 20 MHz | `:756` (fixed_clk1) |

All are `fixed-clock` nodes (`compatible = "fixed-clock"; #clock-cells = <0>`),
so no luofu driver is needed for them.

## Clock gate table (16 gates; `<&crg LUOFU_CLK_*>`)

| idx | `LUOFU_CLK_*` | name | reg-offset | bit | parent | pinned |
|---|---|---|---|---|---|---|
| 0 | `SFC` | `sfc_clk` | 0x14 | 0x00 | `apb_clk` | `:236` |
| 1 | `GPIO0` | `gpio0_clk` | 0x14 | 0x14 | `apb_clk` | `:206` |
| 2 | `GPIO1` | `gpio1_clk` | 0x14 | 0x15 | `apb_clk` | `:216` |
| 3 | `I2C0` | `i2c0_clk` | 0x14 | 0x18 | `apb_clk` | `:226` |
| 4 | `PCIE0` | `pcie0_clk` | 0x20 | 0x0c | `apb_clk` | `:286` |
| 5 | `PCIE1` | `pcie1_clk` | 0x20 | 0x0d | `apb_clk` | `:296` |
| 6 | `GMAC0` | `gemac_clk0` | 0x20 | 0x13 | `gemacN_clk` | `:306` |
| 7 | `LED_PWM` | `led_pwm` | 0x14 | 0x0e | `apb_clk` | `:246` |
| 8 | `LSW_DP` | `lsw_dp_clk0` | 0x20 | 0x00 | `lsw_dp_clk` | `:256` |
| 9 | `LSW_PFE` | `lsw_pfe_clk0` | 0x20 | 0x01 | `lsw_pfe_clk` | `:266` |
| 10 | `MDIO0` | `mdio_clk0` | 0x20 | 0x03 | `gemacN_clk` | `:276` |
| 11 | `GMAC1` | `gemac_clk1` | 0x20 | 0x14 | `gemacN_clk` | `:316` |
| 12 | `GMAC2` | `gemac_clk2` | 0x20 | 0x15 | `gemacN_clk` | `:326` |
| 13 | `GMAC3` | `gemac_clk3` | 0x20 | 0x16 | `gemacN_clk` | `:336` |
| 14 | `GMAC4` | `gemac_clk4` | 0x20 | 0x17 | `gemacN_clk` | `:346` |
| 15 | `PIE` | `pie_clk0` | 0x20 | 0x19 | `ahb_clk` | `:356` |

Gate polarity: `1` = ON (`hi_clk_gate_enable` SETs the bit; no `invert`
property in the pinned gates). Caveat (retained from `clocks2.md` sec 2): the
`0x14`-group `reg-offset` is intact in the pinned body, but each `0x20`-group
offset is reconstructed from the `clk_gate@0020NN` unit address (the
`reg-offset` property renders as `" "`), so those 11 rows stay PROVISIONAL
until bench-confirmed.

## PLLs (read-only; never reprogrammed)

| name | ctrl-offset | status-offset | status-bit | parent | pinned |
|---|---|---|---|---|---|
| `cpu-clk` | 0x198 | 0x90 | 0x1e | `osc` | `:366` |
| `lsw-clk` | 0x1e0 | 0x90 | 0x1b | `osc` | `:378` |

Stage 1 registers these read-only (`recalc_rate` = ref passthrough; `is_enabled`
reads the pinned status word `0x90`). The boot-configured multiplier is not yet
transcribed and the ctrl words are out of bounds.

## Muxes (read-only; `set_parent` refuses)

| name | reg-offset | mask | parents | pinned |
|---|---|---|---|---|
| `cpu_mux` | 0x138 | 0x8 | `cpu-clk`, `lsw-clk` | `:390` |
| `efuse_mux` | 0x138 | 0x2 | `efuse_ring_clk`, `osc` | `:401` |

`0x138` is out of bounds for writes (a store there switches the CPU PLL source;
`wrdesign.md` sec 1), so `get_parent` is a pure read and `set_parent` returns
`-EPERM`.

## Reset controller

`#reset-cells = <2>` with cells `(reg-offset, bit)`; bits live in the CRG page.
Observed pairs already in the pinned tree: `fmc <0x2c 0x0>`, `gpio0/1
<0x2c 0x14/0x15>`, `i2c0 <0x2c 0x18>`, `gemac <0x30 0xd/0xe>`, `pcie
<0x34 0xc..0xf>`. The soft-reset magic `softrst_val0 = <0x51162100>` /
`softrst_val1 = <0xaee9deff>` (`pinned crg@14880000 :479/:480`) must be
reproduced in the reset/reboot path. The lab `.ko` does NOT register a reset
provider yet (that is the next stage; see `build/tmp/inta-spec/drvcrg.md`).

## Access rules carried into the driver

- Dword-aligned accesses only; the single write primitive
  (`luofu_crg_rmw`) refuses a non-4-aligned offset before executing.
- Every store is a read-modify-write of one bit; never a blind `writel`.
- Out of bounds for stores: reboot `0x00`, resume `0x38`, watchdog
  `0x50/0x64/0x70`, softrst `0x84`, mux `0x138`, PLL ctrl `0x198/0x1e0`.
- Never write CA `0x400392f0`; never read `0x10161000` or host-side IAR
  `0x4016010c`.
