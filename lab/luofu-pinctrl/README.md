# luofu-pinctrl

Stage-2 **mapping-table driver** for the Hi5671Y "luofu" peripheral IOMUX,
matching the `hsan,luofu-peri-pinctrl` node in
`opensource/docs/soc/luofu-r116-pinned.dts` (the vendor's `hi_kpinctrl` module).
It carries the pinctrl-driver shape (`of_match_table` + `probe`/`remove` +
`pinctrl_register`) and the vendor mapping transcribed from the shipped module
`rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_kpinctrl.ko` (vermagic
`5.10.201 SMP mod_unload ARMv7`, 17,636 B), cited to the .ko offsets:

- **37 pins** (`pin0`..`pin36`) — pin desc array `.data+0x428`.
- **24 groups** with the **real per-group pin lists** — group table `.data+0x68`
  (40-byte entries), pin-number words `.rodata+0x32c..0x3e4`. `jtag_gpio2..5`
  additionally carry a group-level mux RMW (mask/value of bit 0 of mux
  register 8).
- **24 functions** (1:1 with the groups) — function table `.rodata+0x20c`,
  group-name array `.data+0x5e4`.
- **per-pin mux drv_data** — 37 x 5 entries `{reg_off, shift, func_name}` +
  `cfg_off` at `.data+0x644..0xf84`, used by `hi_pinmux_set_mux` (`.text+0x348`).

The module **compiles** (and performs no register writes) in the CI cross-build
against the vanilla 5.10.201 arm headers, via
`.github/workflows/lab-module-build.yml` (matrix target `lab/luofu-pinctrl`,
triggered on `omo/**`); the resulting `luofu-pinctrl.ko` uploads as the
`luofu-pinctrl-ko` artifact. `set_mux` and the pinconf setters are still
**no-op stubs** (preserving the bootloader/vendor mux state) — the transcribed
tables are data for the next fill-in, not yet executed. Design notes and the
vendor-evidence map: `build/tmp/inta-spec/drvpin.md`.
