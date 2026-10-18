# luofu-r116.dts - placeholder inventory

`luofu-r116.dts` in this directory is a **published, verbatim copy** of the
stage-1 skeleton at
`/opt/kbuild/linux-5.10.201/arch/arm/boot/dts/luofu-r116.dts` (itself copied
from `opensource/docs/soc/luofu-r116.dts` by `build/tmp/wsl_build_kernel.sh`).
The only addition is a 7-line provenance header; the body is byte-identical
(`tail -n +8` compares equal to the build-tree file, sha256
`2a67768925e505496b9ef15553cef5c3a21365dec168aa66d03dea6abc70a90b`).

Nothing here is measured on the device. "Where it would be measured" names the
artifact or quantity a future bench session would read; register addresses are
only the ones this DTS already carries.

## Hits

The case-sensitive command below matches the 8 lines the port marks TODO /
FIXME / placeholder:

    grep -n -E 'TODO|FIXME|placeholder' /opt/kbuild/linux-5.10.201/arch/arm/boot/dts/luofu-r116.dts

8 hits -> 8 rows. Line numbers are the **build-tree** line numbers; the
published copy is offset by +7 (the header), so build line 13 is published
line 20.

| line | property / site | what is missing | where it would be measured |
| ---: | --- | --- | --- |
| 13 | `compatible` strings: vendor `hsan,*` blocks renamed to `hisilicon,luofu-*` (header comment) | The new bindings themselves. Every renamed block starts as a placeholder; the file's own note says each is marked TODO. One measured mismatch is already recorded: the pinctrl node had to change from `hisilicon,luofu-peri-pinctrl` to `hsan,luofu-peri-pinctrl` to match its driver (lines 400-405). | Each node's `compatible` against its driver's `of_match_table`; a silent mismatch produces no probe error. `mach-luofu/luofu.c` matches `.dt_compat = "hisilicon,luofu-r116" / "hisilicon,luofu"` (submodule README, image-lane table). |
| 14 | `status = "disabled"` on the placeholder blocks (header comment) | A driver and a binding that justify enabling each block. The file records which nodes were later enabled after measurement (`pinctrl_peri` 411, `gmac1` 536, `gmac2` 548, `gmac3` 560, `gmac4` 572, `mdio0` 595, `lsw_dp` 630, `lsw_pfe` 653) and which remain disabled (`tvsensor` 290, `uart0` 323, `lsw_woe` 666, `pie` 678, `pcie0` 723, `pcie1` 765). | The live tree's `status` values under `/proc/device-tree` and each node's probe line; a disabled node cannot bind, so "no probe" is not evidence about the driver. |
| 89 | `cpus/enable-method = "hisilicon,hsan_smp"` (also line 90) | A mainline `smp_ops` implementation for the Hisilicon pen/smc path - the TODO calls it the stage-1 SMP port. The pinned tree (`pinned:435`) uses the same method; its register contract is not carried in this DTS. | Secondary-CPU bring-up in the boot log (SMP CPU count) and `/sys/devices/system/cpu/cpu1/online`; the DT value itself at `/proc/device-tree/cpus/enable-method`. |
| 96 | `cpu@0` is missing `clocks`, `cpu-supply`, `operating-points-v2` and `nvmem-cells` | The four properties carried by the pinned `cpu@0` (`pinned:440`, quoted in lines 96-98); reintroducing them needs the luofu clock, efuse and pwm-regulator drivers. | `pinned:440` for the property set; then cpufreq's OPP table, the regulator supply, and the nvmem cells exposed by the efuse driver. No values are guessed here. |
| 255 | `l2-cache` AUX translation | Mainline `cache-l2x0` derives the PL310 AUX register from `arm,tag-latency` / `arm,data-latency`; the pinned node instead carries `l2c_aux_val = <0x430001>`, `l2c_aux_mask = <0xffb0fffe>` and `cache-way-size-sel = <1>` (`pinned:1043`, quoted at lines 253-256). The translation is still missing. | The PL310 AUX value the block ends with at the `l2-cache` reg base `0x7f000000` (line 250), compared with the pinned `0x430001`. The DTS does not state the AUX register offset, so none is named here. `mach-luofu/luofu.c` replays the same pair (`.l2c_aux_val = 0x430001`, `.l2c_aux_mask = ~0`) per the submodule README. |
| 388 | section marker: `vendor-specific blocks: placeholders (new bindings, TODO)` | Bindings for every block under this marker: `pinctrl_peri` 394, `fmc` 435, `gmac0..4` 485/511/539/551/563, `mdio0` 575, `lsw_dp` 604, `lsw_pfe` 635, `lsw_woe` 656, `pie` 671, `pcie0` 686, `pcie1` 731. | Probe/match results for those nodes against the lab drivers (for example `lab/luofu-pinctrl`), and the nodes' `status` in the live tree. |
| 412 | `pinctrl@14900100` binding | The full pin/group/function table: `37 pins / 24 groups / 24 functions` (`pinctrl.md` sec 2, cited at line 413). Only the `spi` group and the `spi-default-state` / `spi-sleep-state` mapping shape are shown (417-429); the pinned DT selects only `spi`, `jtag_gpio2..5`, `led0_1`, `led1_1` and `avs`. | `pinctrl.md` sec 2, cross-checked against the two windows this node's `reg` names: mux `0x14900100` + `0x3c` (15 x 32-bit) and cfg `0x14940000` + `0x100` (64 x 32-bit), lines 396-398. No bit assignment is stated in this DTS. |
| 724 | `pcie@10160000` DWC glue binding | The DWC glue binding itself; the TODO names `pcie-histb.c` / `pcie-hisi.c` as the reference and says our port polls link-up via DBI/cfg, never the misc window. No glue register layout is carried. | DBI and cfg windows: `pcie0` dbi `0x10160000`, cfg `0x50000000`; `pcie1` dbi `0x10164000`, cfg `0x68000000` (lines 695-697, 740-742). **Never** read misc `0x10161000` / `0x10165000` - the file records that a read there panics the box (lines 682-684, 696, 741). |

## Not counted by the grep

- **line 23** says `Placeholder CRG clock IDs` (capital P) and lines 23-47
  explain that no `<dt-bindings/clock/luofu.h>` exists yet: the 16
  `LUOFU_CLK_*` IDs in lines 48-63 stand in for it, and the
  `(reg-offset, bit)` geometry is taken from the pinned clk node. The
  case-sensitive grep does not select it, so it is not a table row. Its
  measurement locus is the CRG at `0x14880000` and the clock table the driver
  builds from these IDs.
- **lines 44-47** mark the 0x20-group reg offsets as *provisional* until
  bench-confirmed; the 0x14-group offsets are described as verbatim from the
  pinned body. That is a caveat, not a TODO/FIXME/placeholder hit, so it is
  not a row either.

## Verify

The published file compiles with the build tree's own toolchain (DTC 1.7.2):

    cd /opt/kbuild/linux-5.10.201
    arm-linux-gnueabihf-cpp -nostdinc -I include -I arch/arm/boot/dts \
      -undef -D__DTS__ -x assembler-with-cpp \
      <repo>/opensource/lab/luofu-kernel/dts/luofu-r116.dts -o /tmp/dts_check.dts
    dtc -I dts -O dtb -o /tmp/dts_check.dtb /tmp/dts_check.dts

Exit 0. The resulting DTB is byte-identical to
`arch/arm/boot/dts/luofu-r116.dtb` (md5 `7be579be137c2e83868e3dcba32f3956`);
the only messages are the six pre-existing `unit_address_vs_reg` warnings.
Row count check: `grep -c '^| [0-9]' dts/README.md` -> 8.
