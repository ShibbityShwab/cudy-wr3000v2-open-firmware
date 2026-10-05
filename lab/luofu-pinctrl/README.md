# luofu-pinctrl

Stage-2 **skeleton** for the Hi5671Y "luofu" peripheral IOMUX, matching the
`hsan,luofu-peri-pinctrl` node in `opensource/docs/soc/luofu-r116-pinned.dts`
(the vendor's `hi_kpinctrl` module). It carries the pinctrl-driver shape
(`of_match_table` + `probe`/`remove` + `pinctrl_register`) and a pin table
transcribed from the shipped module
`rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_kpinctrl.ko`: **37 pins**
(pin0..pin36), **24 groups**, **24 functions** (1:1 with the groups), over the
two `reg-names` windows `"mux"` (`0x14900100`, 0x3c) and `"cfg"`
(`0x14940000`, 0x100).

The module **compiles** (and performs no register writes) in the CI cross-build
against the vanilla 5.10.201 arm headers, via
`.github/workflows/lab-module-build.yml` (target `lab/luofu-pinctrl`, triggered
on `omo/**`) and the `build-load-test-module.yml` lane; the resulting
`luofu-pinctrl.ko` is uploaded as the `luofu-pinctrl-ko` artifact. Still TODO:
the real per-group pin membership and the mux/config register programming
(`set_mux` and the pinconf paths are no-ops), so this is a scaffold that builds
and probes, not yet a bring-up driver. Design notes and vendor evidence:
`build/tmp/inta-spec/pinctrl.md`.
