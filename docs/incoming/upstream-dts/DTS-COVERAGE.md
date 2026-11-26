# Board DTS coverage - luofu R116 (Cudy WR3000 v2.0)

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Lane:** task-238 (upstream-dts), host-only. **Date:** 2026-11-27.
**Status:** single source compiles clean and passes the 24-check board contract;
the extracted vendor base DT is inventoried and diffed; the remaining DTS
upstream gaps are listed in section 6. **No active DTS change is
hardware-validated in this lane** - the only boot-tested DTS line is the
2026-11-24 GP1 image, which predates the 2026-11-29 v4 `reset-gpios` fold
(section 7).

## 1. Single source

| item | value |
| --- | --- |
| board DTS | `opensource/target-skeleton/dts/luofu-r116.dts` - 715 lines, sha256 `6774221e...` |
| loader-match overlay | `opensource/target-skeleton/dts/luofu-r116-overlay.dtso` - 90 lines (no-op `__overlay__` + the three decoded match properties) |
| kernel-side binding/reg headers | target patch 004 (`dt-bindings/clock/hisilicon,luofu-crg.h`) and the driver patches' YAML bindings |
| assembled into the target tree | `tools/assemble-target.sh --apply <openwrt>` installs it as `target/linux/hisilicon/dts/luofu-r116{,-overlay}.dts{,o}` |

The board has **no second DTS**: DTS-0001 (the submission's
`arch/arm/boot/dts/luofu-r116.dts` entry) is generated from this single source
by `upstream/series-6.18/tools/gen-series.py` and checked byte-identically.
This lane adds no copy.

## 2. Build evidence (host cpp + dtc, 2026-11-27)

`validation/dts/build-evidence.txt`, `validation/dts/dtc-warnings.log`:

- source 715 lines sha256 `6774221ef49b10888bfb277f2c7620858a798a3cf228b1660cb483df50e6b062`;
- `cpp` (dt-bindings from the prepared `linux-6.18.60` target kernel) + host `dtc` v1.8.1:
  **DTC-RC=0, zero warnings**, decompiled tree 687 lines;
- **DTB 12,848 bytes, sha256 `1c200daf8046cb154e5e6dfa990cd827348ab960da9fce13596ebfa0bb4e04b8`** -
  the same size/`sha256` the 2026-11-29 v4 submission note records
  (`upstream/series-6.18/07-dts-upstream-bindings.md`, v4 status), so the host
  DTB and the review-form DTS-0001 payload are the same content;
- the full image build compiles this source through the stock
  `Image/BuildDTB` rule (`DEVICE_DTS := luofu-r116`, `DEVICE_DTS_DIR := ../dts`)
  and pads it with `scripts/pad-dtb.py` before the FIP slice assembly
  (`validation/build/` - assembled target build).

## 3. Requirement coverage - 24/24 checks PASS

`tools/check-board-dts.py` queries the **compiled DTB** with `fdtget` (not the
source text), so a preprocessor/label mistake cannot pass silently. Matrix in
`validation/dts/dts-check.log`; grouped:

| group | checks | verified fact (from the DTB) |
| --- | --- | --- |
| identity | R1, R2 | `model` names the board; `compatible = "cudy,wr3000-v2", "hisilicon,luofu-r116", "hisilicon,luofu"` |
| CPU / SMP | C1, C2 | `cpus.enable-method = "hisilicon,luofu-smp"`; exactly 2 CPUs, both `arm,cortex-a9` |
| memory | M1, M2 | DDR3 window `0x80500000` size `0x7b00000`; `flashinfo@80600000` reserved (atag offset 0x1000) |
| UART | U1, U2, U3 | console = UART1 `serial@1010f000` (`snps,dw-apb-uart`, okay), `stdout-path = "serial0:115200n8"`, `serial0` alias; UART0 present, disabled |
| NAND | N1, N2 | FMC `spi@10a20000` (`hisilicon,luofu-fmc`, okay) + `spi-nand` child; **17** fixed partitions, `kernela` = `0x840000` (8,650,752 B), `rootfsa` = `0x1740000` (24,313,856 B) - exactly the image recipe's slot sizes |
| power/reset | P1, P2 | CRG `system-controller@14880000` (`hisilicon,luofu-crg`, syscon/simple-mfd, #clock-cells=1, #reset-cells=2) + `hisilicon,luofu-wdt` child; sysctrl + `luofu-reboot` + `luofu-boot-selector` (selector offset `0xc08`) |
| interrupt/timer | I1 | GIC (`arm,cortex-a9-gic`), SCU, PL310, TWD timer, SP804 timer |
| GPIO/keys/LEDs | G1, G2 | two `snps,dw-apb-gpio` banks with port children; `gpio-keys-polled` reset+wps; 6 LEDs |
| PCIe | E1, E2 | RC0/RC1 `hisilicon,luofu-pcie`, domains 0/1, 3 reg regions (`dbi`,`misc`,`config`), 1 `ranges`, 1 `dma-ranges` (device DMA 0x8xxxxxxx -> DRAM), 2 interrupts (`radm`,`linkdown`); RC0 carries the folded 3-line `reset-gpios` (GPIO0 20/21/28 active low) + 20 ms/100 ms timings |
| network | D1, D2, S1, S2 | MDIO `hisilicon,luofu-mdio` + 4 PHYs; 5 GEMAC nodes; switch `hisilicon,luofu-switch` okay with 4 user ports; `hisilicon,luofu-pie` okay; WOE node present disabled |
| pinctrl/thermal | T1 | `hisilicon,luofu-pinctrl`; `hisilicon,luofu-tvsensor` + `thermal-zones` with 2 trips |
| clocks | K1 | 8 fixed clocks, `twd-clock`, CRG clock wiring (7 named clocks) |

**128 MB DDR3, why the DTB says 123 MiB.** The board has 128 MiB DDR3
(`0x80000000..0x88000000`). The stock loader reserves the first 5 MiB for its
own staging/FIP-ATAG area and Linux is given `0x80500000 + 0x7b00000`
(123 MiB); the `flashinfo` reserved region at `0x80600000` carries the loader
hand-off ATAG at offset 0x1000. This is the measured layout, not a truncation
bug - changing it needs a boot-test.

## 4. Extracted vendor DT comparison

Local reference (gitignored, **not** committed):
`dumps/router-20261108/kernelb/kernelb.dtb`, 13,880 B - the **static base**
board DT shipped in flash. The stock loader applies the vendor R116 overlay
(LEDs/keys/Wi-Fi endpoints) at runtime, so those board bits are not in this
base DTB. Full run: `validation/dts/dts-check.log` (node-path diff +
compatible-set diff); provenance: `validation/dts/vendor-base-provenance.txt`.

Both trees have 92 nodes. The node-path diff is dominated by renames
(`hsan,crg` -> `hisilicon,luofu-crg`, `spi-nand@10a20000` -> `spi@10a20000`
FMC wrapper, `ethernet-switch@{87b00000,12100000}` -> one
`ethernet-switch@11000000`, `tvsensor@14900500` -> `thermal-sensor@14900500`,
`cls0` -> `cls0-thermal`), so the **compatible-set diff is the useful view**:

- **vendor-only (8)**: `hsan,crg`, `hsan,clk-gate`, `hisilicon,luofu-clk-pll`,
  `hisilicon,luofu-clk-mux`, `hsan,luofu-peri-pinctrl`, `hisilicon,luofu-dp`,
  `hisilicon,luofu-pfe`, and the root pair without `cudy,wr3000-v2`.
  Meaning: the vendor DT models the CRG clock tree as explicit `clk-gate`/
  `clk-pll`/`clk-mux` devices; our port registers the PLLs/gates inside the
  CRG clock driver (patch 019) and keeps only fixed clocks in DT; the vendor
  switch datapath (`dp`,`pfe`) is one merged `luofu-switch` node in our model.
  These are review items for the binding split, not missing hardware.
- **board-only (11)**: `cudy,wr3000-v2` root identity; the sysctrl/reboot/
  boot-selector split nodes; `hisilicon,luofu-crg` syscon/simple-mfd form;
  `hisilicon,luofu-pinctrl`; `hisilicon,luofu-switch`; `spi-nand` standard
  child; the flashinfo reserved-memory node (with the vendor fallback string);
  and `gpio-keys-polled`/`gpio-leds` (in the vendor **overlay**, absent from
  the vendor base, present in our base because our DTS is the complete board).

## 5. What this lane does NOT prove

- The DTB content is not hardware-validated **in this form**: the 2026-11-29
  v4 fold (RC0 `reset-gpios` + timings replacing the retired
  `wifi-pcie-hog`) is an active-DTS change and needs one attended boot
  (single-source header; `07-dts-upstream-bindings.md` v4 note).
- No `dtbs_check` run: the in-series YAML bindings are not yet in a tree with
  dtschema; the custom `hisilicon,luofu-*` bindings are drafts/RFCs.
- The loader hand-off path (base DTB + overlay container in the FIP trailer) is
  proven by the image assembly and structurally validated, not by a boot of
  this rebuild.

## 6. Remaining DTS upstream gaps (for the target PR / DTS-0001 review)

1. **Vendor fallback compatible** - `flashinfo@80600000` keeps
   `"hsan,flashinfo_reserved"` as the second string (upstream string first;
   hardware-validated first-compatible change, image 6e9c54d0). A stricter
   upstream form needs either a documented vendor fallback or its removal plus
   a boot-test.
2. **`acp-gate`** - custom `pcie@10160000` property (register/gate/value) that
   the review must convert to a proper ACP power/clock domain.
3. **Custom bindings** - all `hisilicon,luofu-*` blocks are drafted/RFC YAML;
   the DTS is only as upstreamable as those bindings.
4. **pinctrl consumer states** - the board DTS has no `pinctrl-0/1` states on
   consumers; the pinctrl driver carries tables instead. Mainline style wants
   named pin states for at least UART/NAND/MDIO/GEMAC.
5. **Clock tree split** - PLLs/muxes live in the CRG driver (patch 019), not in
   DT; decide driver-internal providers vs separate `clk` nodes before posting.
6. **Optional deltas kept deliberately** - key/LED child names, extra
   `status = "okay"` properties, `serial1` alias, SP804/WOE nodes: each is a
   boot-tested-or-not decision, documented in `07-dts-upstream-bindings.md`
   section 3.
7. **Disabled nodes on purpose** - `uart0`, `i2c0`, `gmac0`, WOE are present but
   disabled; the enabled GEMAC/switch/PIE nodes describe the bench-verified
   CPU-copy mirror path, not native switch forwarding (do not present as a
   working LAN switch).

## 7. Reproduce

~~~
# from opensource/docs/incoming/upstream-dts
tools/assemble-target.sh --apply /home/shibbityshwab/openwrt-lab-upstream-target
# (that tree already has linux-6.18.60 prepared; the image build compiles the DTS)
tools/check-board-dts.py --dtb <openwrt>/build_dir/target-*/linux-*/image-luofu-r116.dtb \
    --vendor-dtb <parent>/dumps/router-20261108/kernelb/kernelb.dtb
~~~
