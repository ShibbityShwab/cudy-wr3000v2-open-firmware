# luofu-clk

Stage-1 **skeleton** (NOT-YET-COMPILED) for the Hi5671Y "luofu" CRG clock +
reset controller, matching the `hisilicon,luofu-crg` node in
`opensource/docs/soc/luofu-r116.dts`. It carries the platform-driver shape
(`of_match_table` + `probe`/`remove`) and the regmap plan from
`build/tmp/inta-spec/clocks2.md`: one shared `devm_platform_ioremap_resource`
of the 0x1000 CRG page (no regmap for stage 1), with the pinned gate/mux/PLL
tables and `softrst_val0/1` transcribed from `luofu-r116-pinned.dts`. The clock
registration and reset-provider wiring are TODO until the hisi clk/reset helpers
are brought in; the module is a design scaffold, not a loadable .ko yet.
