# luofu-clk

Stage-1 **skeleton** for the Hi5671Y "luofu" CRG clock + reset controller,
matching the `hisilicon,luofu-crg` node in `opensource/docs/soc/luofu-r116.dts`.
It carries the platform-driver shape (`of_match_table` + `probe`/`remove`) and
the regmap plan from `build/tmp/inta-spec/clocks2.md`: one shared
`devm_platform_ioremap_resource` of the 0x1000 CRG page (no regmap for stage 1),
with the pinned gate/mux/PLL tables and `softrst_val0/1` transcribed from
`luofu-r116-pinned.dts`.

The module **compiles** (and performs no register writes) in the CI cross-build
against the vanilla 5.10.201 arm headers, via
`.github/workflows/lab-module-build.yml` (target `lab/luofu-clk`, triggered on
`omo/**`) and the `build-load-test-module.yml` lane; the resulting
`luofu-clk.ko` is uploaded as the `luofu-clk-ko` artifact. Still TODO: the real
`hisi_clk_*` gate/mux/PLL registration and the reset-provider wiring
(`hisi_reset_init`), so this is a scaffold that builds and probes, not yet a
bring-up driver.
